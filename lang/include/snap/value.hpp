#pragma once
#include "block.hpp"
#include "forward.hpp"
#include "token.hpp"
#include <string>

namespace snap {

enum class ObjType : u8 {
	string,
	codeblock,
	closure,
	c_closure,
	upvalue,
	table,
	user_data,
};

/// Objects always live on the heap. A value which is an object contains a pointer
/// to this data on the heap. The `tag` specifies what kind of object this is.
class Obj {
	// The VM and the Garbage Collector
	// need access to the mark bit and the
	// next pointer. So we'll declare them
	// as friend classes.
	friend VM;
	friend GC;
	friend Table;

public:
	const ObjType tag;

	explicit Obj(ObjType tt) noexcept : tag{tt} {};
	Obj(Obj&& o) = default;
	Obj(Obj const& o) = default;
	virtual ~Obj() = default;

	virtual const char* to_cstring() const;

protected:
	/// @brief pointer to the next object in the VM's GC linked list.
	Obj* next = nullptr;
	/// @brief Whether this object has been 'marked' as alive in the most
	/// currently active garbage collection cycle (if any).
	bool marked = false;

	/// @brief Traces all the references that this object
	/// contains to other values. Is overriden by deriving
	/// class.
	virtual void trace(GC& gc) = 0;

	/// @brief returns the size of this object in bytes.
	virtual size_t size() const = 0;
};

enum class ValueType { Number, Bool, Object, Nil, Undefined };

// Without NaN tagging, values are represented
// as structs weighing 16 bytes. 1 word for the
// type tag and one for the union representing the
// possible states. This is a bit wasteful but
// not that bad.
/// TODO: Implement optional NaN tagging when a macro
/// SNAP_NAN_TAGGING is defined.
struct Value {
	ValueType tag;
	union Data {
		number num;
		bool boolean;
		Obj* object;
		Data(){};
		Data(number v) noexcept : num(v){};
		Data(bool b) noexcept : boolean(b){};
		Data(Obj* o) noexcept : object(o){};
	} as;

	explicit Value(number n) noexcept : tag{ValueType::Number}, as{n} {};
	explicit Value(bool b) noexcept : tag{ValueType::Bool}, as{b} {};
	explicit Value() noexcept : tag{ValueType::Nil} {};
	explicit Value(Obj* o) noexcept : tag{ValueType::Object}, as{o} {
		SNAP_ASSERT(o != nullptr, "Unexpected nullptr object");
	}

	static inline Value undefined() {
		Value undef;
		undef.tag = ValueType::Undefined;
		return undef;
	}
};

bool operator==(const Value& a, const Value& b);
bool operator!=(const Value& a, const Value& b);

// It might seem redundant to represent
// these procedures as free functions instead
// of methods, but once we have NaN tagging, we
// would still like to have the same procedure
// signatures used across the codebase.
std::string value_to_string(Value v);
char* value_to_cstring(Value v);
const char* value_type_name(Value v);
void print_value(Value v);

#define SNAP_SET_NUM(v, i)		((v).as.num = i)
#define SNAP_SET_BOOL(v, b)		((v).as.boolean = b)
#define SNAP_SET_OBJECT(v, o) ((v).as.object = o)

#define SNAP_NUM_VAL(n)		 (snap::Value(static_cast<snap::number>(n)))
#define SNAP_BOOL_VAL(b)	 (snap::Value(static_cast<bool>(b)))
#define SNAP_OBJECT_VAL(o) (snap::Value(static_cast<snap::Obj*>(o)))
#define SNAP_NIL_VAL			 (snap::Value())
#define SNAP_UNDEF_VAL		 (snap::Value::undefined())

#define SNAP_SET_TT(v, tt)		((v).tag = tt)
#define SNAP_GET_TT(v)				((v).tag)
#define SNAP_CHECK_TT(v, tt)	((v).tag == tt)
#define SNAP_ASSERT_TT(v, tt) (SNAP_ASSERT(SNAP_CHECK_TT((v), tt), "Mismatched type tags."))
#define SNAP_ASSERT_OT(v, ot)                                                                      \
	(SNAP_ASSERT((SNAP_AS_OBJECT(v)->tag == ot), "Mismatched object types."))
#define SNAP_TYPE_CSTR(v) (value_type_name(v))

#define SNAP_IS_NUM(v)			 ((v).tag == snap::ValueType::Number)
#define SNAP_IS_BOOL(v)			 ((v).tag == snap::ValueType::Bool)
#define SNAP_IS_NIL(v)			 ((v).tag == snap::ValueType::Nil)
#define SNAP_IS_UNDEFINED(v) ((v).tag == snap::ValueType::Undefined)
#define SNAP_IS_OBJECT(v)		 ((v).tag == snap::ValueType::Object)
#define SNAP_IS_STRING(v)		 (SNAP_IS_OBJECT(v) and SNAP_AS_OBJECT(v)->tag == snap::ObjType::string)
#define SNAP_IS_TABLE(v)		 (SNAP_IS_OBJECT(v) and SNAP_AS_OBJECT(v)->tag == snap::ObjType::table)

#define SNAP_AS_NUM(v)			((v).as.num)
#define SNAP_AS_BOOL(v)			((v).as.boolean)
#define SNAP_AS_NIL(v)			((v).as.double)
#define SNAP_AS_OBJECT(v)		((v).as.object)
#define SNAP_AS_CLOSURE(v)	(static_cast<snap::Closure*>(SNAP_AS_OBJECT(v)))
#define SNAP_AS_CCLOSURE(v) (static_cast<snap::CClosure*>(SNAP_AS_OBJECT(v)))
#define SNAP_AS_PROTO(v)		(static_cast<snap::CodeBlock*>(SNAP_AS_OBJECT(v)))
#define SNAP_AS_STRING(v)		(static_cast<snap::String*>(SNAP_AS_OBJECT(v)))
#define SNAP_AS_CSTRING(v)	(SNAP_AS_STRING(v)->c_str())
#define SNAP_AS_TABLE(v)		(static_cast<Table*>(SNAP_AS_OBJECT(v)))

#define SNAP_CAST_INT(v) (s64(SNAP_AS_NUM(v)))

} // namespace snap