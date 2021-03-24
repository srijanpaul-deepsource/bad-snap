#include "str_format.hpp"
#include <cmath>
#include <common.hpp>
#include <compiler.hpp>
#include <cstdarg>
#include <cstdio>
#include <function.hpp>
#include <stdio.h>
#include <string.hpp>
#include <upvalue.hpp>
#include <value.hpp>
#include <vm.hpp>

#if defined(SNAP_DEBUG_RUNTIME) || defined(SNAP_DEBUG_DISASSEMBLY)
#include <debug.hpp>
#endif

#define ERROR(...)	   runtime_error(kt::format_str(__VA_ARGS__))
#define CURRENT_LINE() (m_current_block->lines[ip - 1])

#define FETCH()		(m_current_block->code[ip++])
#define NEXT_BYTE() (static_cast<u8>(m_current_block->code[ip++]))
#define FETCH_SHORT()                                                                              \
	(ip += 2, (u16)((static_cast<u8>(m_current_block->code[ip - 2]) << 8) |                        \
					static_cast<u8>(m_current_block->code[ip - 1])))
#define READ_VALUE()		  (m_current_block->constant_pool[NEXT_BYTE()])
#define GET_VAR(index)		  (m_current_frame->base[index])
#define SET_VAR(index, value) (m_current_frame->base[index] = value)

// PEEK(1) fetches the topmost value in the stack.
#define PEEK(depth) sp[-depth]
#define POP()		*(--sp)

namespace snap {

using Op = Opcode;
using VT = ValueType;
using OT = ObjType;

VM::VM(const std::string* src) : m_source{src} {
}

#define IS_VAL_FALSY(v)	 ((SNAP_IS_BOOL(v) and !(SNAP_AS_BOOL(v))) or SNAP_IS_NIL(v))
#define IS_VAL_TRUTHY(v) (!IS_VAL_FALSY(v))

#define UNOP_ERROR(op, v) ERROR("Cannot use operator '{}' on type '{}'.", op, SNAP_TYPE_CSTR(v))

#define CMP_OP(op)                                                                                 \
	do {                                                                                           \
		Value b = pop();                                                                           \
		Value a = pop();                                                                           \
                                                                                                   \
		if (SNAP_IS_NUM(a) and SNAP_IS_NUM(b)) {                                                   \
			push(SNAP_BOOL_VAL(SNAP_AS_NUM(a) op SNAP_AS_NUM(b)));                                 \
		} else {                                                                                   \
			return binop_error(#op, b, a);                                                         \
		}                                                                                          \
	} while (false);

#define BINOP(op)                                                                                  \
	do {                                                                                           \
		Value& a = PEEK(1);                                                                        \
		Value& b = PEEK(2);                                                                        \
                                                                                                   \
		if (SNAP_IS_NUM(a) and SNAP_IS_NUM(b)) {                                                   \
			SNAP_SET_NUM(b, SNAP_AS_NUM(b) op SNAP_AS_NUM(a));                                     \
			pop();                                                                                 \
		} else {                                                                                   \
			return binop_error(#op, b, a);                                                         \
		}                                                                                          \
	} while (false);

#define BIT_BINOP(op)                                                                              \
	Value& b = PEEK(1);                                                                            \
	Value& a = PEEK(2);                                                                            \
                                                                                                   \
	if (SNAP_IS_NUM(a) and SNAP_IS_NUM(b)) {                                                       \
		SNAP_SET_NUM(a, SNAP_CAST_INT(a) op SNAP_CAST_INT(b));                                     \
		pop();                                                                                     \
	} else {                                                                                       \
		return binop_error(#op, a, b);                                                             \
	}

#ifdef SNAP_DEBUG_RUNTIME
void print_stack(Value* stack, size_t sp) {
	printf("(%zu)[ ", sp);
	for (Value* v = stack; v < stack + sp; v++) {
		printf("%s", v->name_str().c_str());
		printf(" ");
	}
	printf("]\n");
}
#endif

ExitCode VM::run() {

	while (true) {
		const Op op = FETCH();
#ifdef SNAP_DEBUG_RUNTIME
		disassemble_instr(*m_current_block, op, ip - 1);
#endif

		switch (op) {
		case Op::load_const: push(READ_VALUE()); break;
		case Op::load_nil: push(SNAP_NIL_VAL); break;

		case Op::pop: pop(); break;
		case Op::add: BINOP(+); break;
		case Op::sub: BINOP(-); break;
		case Op::mult: BINOP(*); break;

		case Op::gt: CMP_OP(>); break;
		case Op::lt: CMP_OP(<); break;
		case Op::gte: CMP_OP(>=); break;
		case Op::lte: CMP_OP(<=); break;

		case Op::div: {
			Value& a = PEEK(1);
			Value& b = PEEK(2);

			if (SNAP_IS_NUM(a) and SNAP_IS_NUM(b)) {
				if (SNAP_AS_NUM(b) == 0) {
					return runtime_error("Attempt to divide by 0.\n");
				}
				SNAP_SET_NUM(b, SNAP_AS_NUM(b) / SNAP_AS_NUM(a));
				pop();
			} else {
				return binop_error("/", b, a);
			}
			break;
		}

		case Op::mod: {
			Value& a = PEEK(1);
			Value& b = PEEK(2);

			if (SNAP_IS_NUM(a) and SNAP_IS_NUM(b)) {
				SNAP_SET_NUM(b, fmod(SNAP_AS_NUM(b), SNAP_AS_NUM(a)));
			} else {
				return binop_error("%", b, a);
			}

			pop();
			break;
		}

		case Op::lshift: {
			BIT_BINOP(<<);
			break;
		}

		case Op::rshift: {
			BIT_BINOP(>>);
			break;
		}

		case Op::band: {
			BIT_BINOP(&);
			break;
		}

		case Op::bor: {
			BIT_BINOP(|);
			break;
		}

		case Op::eq: {
			Value a = pop();
			Value b = pop();
			push(SNAP_BOOL_VAL(a == b));
			break;
		}

		case Op::neq: {
			Value a = pop();
			Value b = pop();
			push(SNAP_BOOL_VAL(a != b));
			break;
		}

		case Op::negate: {
			Value& operand = PEEK(1);
			if (SNAP_IS_NUM(operand))
				SNAP_SET_NUM(operand, -SNAP_AS_NUM(operand));
			else
				UNOP_ERROR("-", operand);
			break;
		}

		case Op::lnot: {
			Value a = pop();
			push(SNAP_BOOL_VAL(IS_VAL_FALSY(a)));
			break;
		}

		case Op::jmp_if_true_or_pop: {
			Value& top = PEEK(1);
			if (IS_VAL_TRUTHY(top)) {
				ip += FETCH_SHORT();
			} else {
				ip += 2;
				pop();
			}
			break;
		}

		case Op::jmp_if_false_or_pop: {
			Value& top = PEEK(1);
			if (IS_VAL_FALSY(top)) {
				ip += FETCH_SHORT();
			} else {
				ip += 2;
				pop();
			}
			break;
		}

		case Op::jmp: {
			ip += FETCH_SHORT();
			break;
		}

		case Op::get_var: {
			u8 idx = NEXT_BYTE();
			push(GET_VAR(idx));
			break;
		}

		case Op::set_var: {
			u8 idx = NEXT_BYTE();
			SET_VAR(idx, peek(0));
			break;
		}

		case Op::set_upval: {
			u8 idx											= NEXT_BYTE();
			*m_current_frame->func->get_upval(idx)->m_value = PEEK(1);
			break;
		}

		case Op::get_upval: {
			u8 idx = NEXT_BYTE();
			push(*m_current_frame->func->get_upval(idx)->m_value);
			break;
		}

		case Op::close_upval: {
			close_upvalues_upto(sp - 1);
			pop();
			break;
		}

		case Op::concat: {
			Value& a = PEEK(2);
			Value b	 = pop();

			if (!(SNAP_IS_STRING(a) and SNAP_IS_STRING(b))) {
				return binop_error("..", a, b);
			} else {
				auto left	  = SNAP_AS_STRING(a);
				auto right	  = SNAP_AS_STRING(b);
				size_t length = left->len() + right->len();

				char* buf	= new char[length + 1];
				buf[length] = '\0';

				std::memcpy(buf, left->c_str(), left->len());
				std::memcpy(buf + left->len(), right->c_str(), right->len());

				size_t hash		 = hash_cstring(buf, length);
				String* interned = interned_strings.find_string(buf, length, hash);

				if (interned == nullptr) {
					SNAP_SET_OBJECT(a, &make<String>(buf, hash));
					interned_strings.set(a, SNAP_BOOL_VAL(true));
				} else {
					delete[] buf;
					SNAP_SET_OBJECT(a, interned);
				}
			}
			break;
		}

		case Op::new_table: {
			push(SNAP_OBJECT_VAL(&make<Table>()));
			break;
		}

		case Op::table_add_field: {
			Value value = pop();
			Value key	= pop();

			SNAP_AS_TABLE(PEEK(1))->set(key, value);
			break;
		}

		// table[key] = value
		case Op::index_set: {
			Value value = pop();
			Value key	= pop();

			Value& tvalue = PEEK(1);
			if (SNAP_IS_TABLE(tvalue)) {
				if (SNAP_GET_TT(key) == VT::Nil) {
					return ERROR("Table key cannot be nil.");
				}
				SNAP_AS_TABLE(tvalue)->set(key, value);
				sp[-1] = value; // assignment returns it's RHS.
			} else {
				return ERROR("Attempt to index a {} value.", SNAP_TYPE_CSTR(tvalue));
			}
			break;
		}

		// table.key = value
		case Op::table_set: {
			const Value& key = READ_VALUE();
			Value value		 = pop();
			Value& tvalue	 = PEEK(1);
			if (SNAP_IS_TABLE(tvalue)) {
				SNAP_AS_TABLE(tvalue)->set(key, value);
				sp[-1] = value; // assignment returns it's RHS
			} else {
				return ERROR("Attempt to index a {} value.", SNAP_TYPE_CSTR(tvalue));
			}
			break;
		}

		// table.key
		case Op::table_get: {
			// TOS = as_table(TOS)->get(READ_VAL())
			if (SNAP_IS_TABLE(PEEK(1))) {
				sp[-1] = SNAP_AS_TABLE(PEEK(1))->get(READ_VALUE());
			} else {
				return ERROR("Attempt to index a {} value.", SNAP_TYPE_CSTR(PEEK(1)));
			}
			break;
		}

		// table.key
		case Op::table_get_no_pop: {
			// push((TOS)->get(READ_VAL()))
			if (SNAP_IS_TABLE(PEEK(1))) {
				push(SNAP_AS_TABLE(PEEK(1))->get(READ_VALUE()));
			} else {
				return ERROR("Attempt to index a {} value.", SNAP_TYPE_CSTR(PEEK(1)));
			}
			break;
		}

		// table_or_array[key]
		case Op::index: {
			Value key = pop();
			if (SNAP_IS_TABLE(PEEK(1))) {
				if (SNAP_GET_TT(key) == VT::Nil) return runtime_error("Table key cannot be nil.");
				sp[-1] = SNAP_AS_TABLE(PEEK(1))->get(key);
			} else {
				return ERROR("Attempt to index a {} value.", SNAP_TYPE_CSTR(PEEK(1)));
			}
			break;
		}

		// table_or_array[key]
		case Op::index_no_pop: {
			Value& vtable	 = PEEK(2);
			const Value& key = PEEK(1);
			if (SNAP_IS_TABLE(vtable)) {
				if (SNAP_GET_TT(key) == VT::Nil) return runtime_error("Table key cannot be nil.");
				push(SNAP_AS_TABLE(vtable)->get(key));
			} else {
				return ERROR("Attempt to index a {} value.", SNAP_TYPE_CSTR(vtable));
			}
			break;
		}

		case Op::pop_jmp_if_false: {
			ip += IS_VAL_FALSY(PEEK(1)) ? FETCH_SHORT() : 2;
			pop();
			break;
		}

		case Op::call_func: {
			u8 argc		= NEXT_BYTE();
			Value value = PEEK(argc - 1);
			if (!call(value, argc)) return ExitCode::RuntimeError;
			break;
		}

		case Op::return_val: {
			Value result = pop();
			close_upvalues_upto(m_current_frame->base);
			sp = m_current_frame->base + 1;

			pop();
			push(result);

			m_frame_count--;
			if (m_frame_count == 0) {
				return_value = result;
				return ExitCode::Success;
			}

			m_current_frame = &m_frames[m_frame_count - 1];
			m_current_block = &m_current_frame->func->m_proto->block();
			ip				= m_current_frame->ip;

			break;
		}

		case Op::make_func: {
			Prototype* proto = static_cast<Prototype*>(SNAP_AS_OBJECT(READ_VALUE()));
			u32 num_upvals = NEXT_BYTE();
			Function* func	 = &make<Function>(proto, num_upvals);

			push(SNAP_OBJECT_VAL(func));

			for (u8 i = 0; i < num_upvals; ++i) {
				bool is_local = NEXT_BYTE();
				u8 index	  = NEXT_BYTE();

				if (is_local) {
					func->set_upval(i, capture_upvalue(m_current_frame->base + index));
				} else {
					func->set_upval(i, (m_current_frame->func->get_upval(index)));
				}
			}

			break;
		}

		default: {
			std::cout << "not implemented " << int(op) << " yet" << std::endl;
			return ExitCode::RuntimeError;
		}
		}
#ifdef SNAP_DEBUG_RUNTIME
		print_stack(m_stack, sp - m_stack);
		printf("\n");
#endif
	}

	return ExitCode::Success;
}

#undef FETCH
#undef FETCH_SHORT
#undef NEXT_BYTE
#undef READ_VALUE
#undef GET_VAR
#undef SET_VAR
#undef BINOP
#undef BINOP_ERROR
#undef IS_VAL_TRUTHY
#undef CMP_OP
#undef PEEK

ExitCode VM::interpret() {
	init();
	return run();
}

bool VM::init() {
	Compiler compiler{this, m_source};
	m_compiler = &compiler;

	Prototype* proto = m_compiler->compile();

	// There are no reachable references to `proto`
	// when we allocate `func`. Since allocating a func
	// can trigger a garbage collection cycle, we protect
	// the proto.
	gc_protect(proto);
	Function* func = &make<Function>(proto, 0);

	// Once the function has been made, proto can
	// be reached via `func->m_proto`, so we can
	// unprotect it.
	gc_unprotect(proto);

	push(SNAP_OBJECT_VAL(func));
	callfunc(func, 0);

#ifdef SNAP_DEBUG_DISASSEMBLY
	disassemble_block(func->name()->c_str(), *m_current_block);
	printf("\n");
#endif

	m_compiler = nullptr;
	return true;
}

using OT = ObjType;

Upvalue* VM::capture_upvalue(Value* slot) {
	// start at the head of the linked list
	Upvalue* current = m_open_upvals;
	Upvalue* prev	 = nullptr;

	// keep going until we reach a slot whose
	// depth is lower than what we've been looking
	// for, or until we reach the end of the list.
	while (current != nullptr and current->m_value < slot) {
		prev	= current;
		current = current->next_upval;
	}

	// We've found an upvalue that was
	// already capturing a value at this stack
	// slot, so we reuse the existing upvalue
	if (current != nullptr and current->m_value == slot) return current;

	// We've reached a node in the list where the previous node is above the
	// slot we wanted to capture, but the current node is deeper.
	// Meaning `slot` points to a new value that hasn't been captured before.
	// So we add it between `prev` and `current`.
	Upvalue* upval	  = &make<Upvalue>(slot);
	upval->next_upval = current;

	// prev is null when there are no upvalues.
	if (prev == nullptr) {
		m_open_upvals = upval;
	} else {
		prev->next_upval = upval;
	}

	return upval;
}

void VM::close_upvalues_upto(Value* last) {
	while (m_open_upvals != nullptr and m_open_upvals->m_value >= last) {
		Upvalue* current = m_open_upvals;
		// these two lines are the last rites of an
		// upvalue, closing it.
		current->closed	 = *current->m_value;
		current->m_value = &current->closed;
		m_open_upvals	 = current->next_upval;
	}
}

bool VM::call(Value value, u8 argc) {
	switch (SNAP_GET_TT(value)) {
	case VT::Object: {
		switch (SNAP_AS_OBJECT(value)->tag) {
		case OT::func: return callfunc(SNAP_AS_FUNCTION(value), argc);
		default: ERROR("Attempt to call a {} value.", SNAP_TYPE_CSTR(value)); return false;
		}
		break;
	}
	default: ERROR("Attempt to call a {} value.", SNAP_TYPE_CSTR(value));
	}

	return false;
}

bool VM::callfunc(Function* func, int argc) {
	int extra = argc - func->m_proto->param_count();

	// extra arguments are ignored and
	// arguments that aren't provded are replaced with nil.
	if (extra < 0) {
		while (extra < 0) {
			push(SNAP_NIL_VAL);
			argc++;
			extra++;
		}
	} else {
		while (extra > 0) {
			pop();
			argc--;
			extra--;
		}
	}

	// Save the current instruction pointer
	// in the call frame so we can resume
	// execution when the function returns.
	m_current_frame->ip = ip;
	// prepare the next call frame
	m_current_frame		  = &m_frames[m_frame_count++];
	m_current_frame->func = func;
	m_current_frame->base = sp - argc - 1;
	// start from the first op code
	ip				= 0;
	m_current_block = &func->m_proto->block();
	return true;
}

String& VM::string(const char* chars, size_t length) {
	size_t hash = hash_cstring(chars, length);

	// If an identical string has already been created, then
	// return a reference to the existing string instead.
	String* interned = interned_strings.find_string(chars, length, hash);
	if (interned != nullptr) return *interned;

	String* string = &make<String>(chars, length, hash);
	interned_strings.set(SNAP_OBJECT_VAL(string), SNAP_BOOL_VAL(true));

	return *string;
}

// 	-- Garbage collection --

void VM::gc_protect(Obj* o) {
	m_gc.m_extra_roots.insert(o);
}

void VM::gc_unprotect(Obj* o) {
	m_gc.m_extra_roots.erase(o);
}

void GC::protect(Obj* o) {
	m_extra_roots.insert(o);
}

void GC::unprotect(Obj* o) {
	m_extra_roots.erase(o);
}

void VM::collect_garbage() {
	mark();
	// trace();
	// sweep();
}

void GC::mark(Value v) {
	if (SNAP_IS_OBJECT(v)) {
		mark(SNAP_AS_OBJECT(v));
	}
}

void GC::mark(Obj* o) {
	if (o == nullptr or o->marked) return;
	o->marked = true;
	m_gray_objects.push_back(o);
}

void GC::trace() {
}

void VM::mark() {
	// The following roots are known atm ->
	// 1. The VM's value stack.
	// 2. Every Closure in the call stack.
	// 3. The open upvalue chain.
	// 4. Compiler roots, if the compiler is active.
	// 5. The table of global variables.
	// 6. The 'extra_roots' set.
	for (Value* v = m_stack; v < sp; ++v) {
		m_gc.mark(v);
	}

	for (CallFrame* frame = m_frames; frame < m_current_frame; ++frame) {
		m_gc.mark(frame->func);
	}

	for (Upvalue* uv = m_open_upvals; uv != nullptr; uv = uv->next_upval) {
		m_gc.mark(uv);
	}

	for (Obj* o : m_gc.m_extra_roots) {
		m_gc.mark(o);
	}
}

void VM::trace() {
}

void VM::sweep() {
}

const Block* VM::block() {
	return m_current_block;
}

//  --- Error reporting ---

ExitCode VM::binop_error(const char* opstr, Value& a, Value& b) {
	return ERROR("Cannot use operator '{}' on operands of type '{}' and '{}'.", opstr,
				 SNAP_TYPE_CSTR(a), SNAP_TYPE_CSTR(b));
}

ExitCode VM::runtime_error(std::string&& message) {
	std::string error_str = kt::format_str("[line {}]: {}\n", CURRENT_LINE(), message);

	error_str += "stack trace:\n";
	for (int i = m_frame_count - 1; i >= 0; --i) {
		const CallFrame& frame = m_frames[i];
		const Function& func   = *frame.func;
		int line			   = func.m_proto->block().lines[frame.ip];
		if (i == 0) {
			error_str += kt::format_str("\t[line {}] in {}", line, func.name_cstr());
		} else {
			error_str += kt::format_str("\t[line {}] in function {}.\n", line, func.name_cstr());
		}
	}

	on_error(*this, error_str);
	return ExitCode::RuntimeError;
}

// The default behavior on an error is to simply
// print it to the stderr.
void default_error_fn([[maybe_unused]] const VM& vm, std::string& err_msg) {
	fprintf(stderr, "%s\n", err_msg.c_str());
}

// TODO: FIX THIS MESS
VM::~VM() {
	// if (m_gc_objects == nullptr) return;
	// for (Obj* object = m_gc_objects; object != nullptr; ) {
	// 	print_value(SNAP_OBJECT_VAL(object));
	// 	printf("\n");
	// 	Obj* temp = object->next;
	// 	free_object(object);
	// 	object = temp;
	// }
}

} // namespace snap