#include "value.hpp"
#include <cmath>
#include <common.hpp>
#include <compiler.hpp>
#include <cstdarg>
#include <cstdio>
#include <parser.hpp>
#include <vm.hpp>

#if defined(SNAP_DEBUG_RUNTIME) || defined(SNAP_DEBUG_DISASSEMBLY)
#include <debug.hpp>
#endif

#define FETCH()					(m_block.code[ip++])
#define NEXT_BYTE()				((u8)(m_block.code[ip++]))
#define READ_VALUE()			(m_block.constant_pool[NEXT_BYTE()])
#define SET_VALUE(depth, value) (m_stack[sp - depth - 1] = value)
#define GET_VAR(index)			(m_stack[index])
#define SET_VAR(index, value)	(m_stack[index] = value)

namespace snap {
using Op = Opcode;
using VT = ValueType;

VM::VM(const std::string* src) : source{src}, m_block{Block{}} {};

#define IS_VAL_TRUTHY(v) ((SNAP_IS_BOOL(v) && SNAP_AS_BOOL(v)) || !SNAP_IS_NIL(v))

#define BINOP_ERROR(op, v1, v2)                                                                    \
	runtime_error("Cannot use operator '%s' on operands of type '%s' and '%s'.", op,               \
				  SNAP_TYPE_CSTR(v1), SNAP_TYPE_CSTR(v2))

#define BINOP(op)                                                                                  \
	do {                                                                                           \
		Value& a = m_stack[sp - 1];                                                                \
		Value& b = m_stack[sp - 2];                                                                \
                                                                                                   \
		if (SNAP_IS_NUM(a) && SNAP_IS_NUM(b)) {                                                    \
			SNAP_SET_NUM(b, SNAP_AS_NUM(b) op SNAP_AS_NUM(a));                                     \
			pop();                                                                                 \
		} else {                                                                                   \
			BINOP_ERROR(#op, b, a);                                                                \
		}                                                                                          \
	} while (false);

#define BIT_BINOP(op)                                                                              \
	Value& b = m_stack[sp - 1];                                                                    \
	Value& a = m_stack[sp - 2];                                                                    \
                                                                                                   \
	if (SNAP_IS_NUM(a) && SNAP_IS_NUM(b)) {                                                        \
		SNAP_SET_NUM(a, SNAP_CAST_INT(a) op SNAP_CAST_INT(b));                                     \
		pop();                                                                                     \
	} else {                                                                                       \
		BINOP_ERROR(#op, a, b);                                                                    \
	}

#ifdef SNAP_DEBUG_RUNTIME
void print_stack(Value stack[VM::StackMaxSize], size_t sp) {
	printf("(%zu)[ ", sp);
	for (Value* v = stack; v < stack + sp; v++) {
		printf("%s", v->name_str().c_str());
		printf(" ");
	}
	printf("]\n");
}
#endif

ExitCode VM::run(bool run_till_end) {

	do {
		const Op op = FETCH();

#ifdef SNAP_DEBUG_RUNTIME
		disassemble_instr(m_block, op, ip - 1);
#endif

		switch (op) {
		case Op::load_const: push(READ_VALUE()); break;

		case Op::pop: pop(); break;
		case Op::add: BINOP(+); break;
		case Op::sub: BINOP(-); break;
		case Op::mult: BINOP(*); break;

		case Op::div: {
			Value& a = m_stack[sp - 1];
			Value& b = m_stack[sp - 2];

			if (SNAP_IS_NUM(a) && SNAP_IS_NUM(b)) {
				if (SNAP_AS_NUM(b) == 0) {
					return runtime_error("Attempt to divide by 0.\n");
				}
				SNAP_SET_NUM(b, SNAP_AS_NUM(b) / SNAP_AS_NUM(a));
				pop();
			} else {
				BINOP_ERROR("/", b, a);
			}
			break;
		}

		case Op::mod: {
			Value& a = m_stack[sp - 1];
			Value& b = m_stack[sp - 2];

			if (SNAP_IS_NUM(a) && SNAP_IS_NUM(b)) {
				SNAP_SET_NUM(b, fmod(SNAP_AS_NUM(b), SNAP_AS_NUM(a)));
			} else {
				BINOP_ERROR("%", b, a);
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

		case Op::eq: {
			Value a = pop();
			Value b = pop();
			push(SNAP_BOOL_VAL(Value::are_equal(a, b)));
			break;
		}

		case Op::neq: {
			Value a = pop();
			Value b = pop();
			push(SNAP_BOOL_VAL(!Value::are_equal(a, b)));
			break;
		}

		case Op::jmp: {
			ip = static_cast<size_t>(FETCH());
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

		case Op::concat: {
			Value& a = m_stack[sp - 2];
			Value& b = m_stack[sp - 1];

			if (!(a.is_string() && b.is_string())) {
				return binop_error("..", a, b);
			} else {
				String* s = String::concatenate(a.as_string(), b.as_string());
				a.as.object = s;
			}
			pop();
			break;
		}
		case Op::return_val: return ExitCode::Success;
		default: std::cout << "not implemented yet" << std::endl;
		}
#ifdef SNAP_DEBUG_RUNTIME
		print_stack(m_stack, sp);
		printf("\n");
#endif
	} while (run_till_end);

	return ExitCode::Success;
}

#undef FETCH
#undef NEXT_BYTE
#undef READ_VALUE
#undef SET_VALUE
#undef GET_VAR
#undef SET_VAR
#undef BINOP
#undef BINOP_ERROR
#undef IS_VAL_TRUTHY

ExitCode VM::interpret() {
	init();
	return run();
}

bool VM::init() {
	Compiler compiler{this, source};
	compiler.compile();

#ifdef SNAP_DEBUG_DISASSEMBLY
	disassemble_block(m_block);
	printf("\n");
#endif

	return true;
}

ExitCode VM::binop_error(const char* opstr, Value& a, Value& b) {
	return runtime_error("Cannot use operator '%s' on operands of type '%s' and '%s'.", opstr,
						 SNAP_TYPE_CSTR(a), SNAP_TYPE_CSTR(b));
}

ExitCode VM::runtime_error(const char* fstring, ...) const {
	va_list args;
	va_start(args, fstring);
	default_error_fn(*this, fstring, args);
	va_end(args);
	return ExitCode::RuntimeError;
}

void default_error_fn(const VM& vm, const char* message, va_list args) {
	vfprintf(stderr, message, args);
	fputc('\n', stderr);
}

} // namespace snap