
//
// ===============================================================================
// clReflect
// -------------------------------------------------------------------------------
// Copyright (c) 2011-2012 Don Williamson & clReflect Authors (see AUTHORS file)
// Released under MIT License (see LICENSE file)
// ===============================================================================
//
// TODO:
//    * Allow names to be specified as CRCs?
//    * Escape sequences need converting.
//    * Enums communicated by value (load integer checks for enum - could have a verify mode to ensure the constant exists).
//    * Field names need to be in memory for JSON serialising to work.
//

#include <clutl/Serialise.h>
#include <clutl/JSONLexer.h>
#include <clcpp/Containers.h>
#include <clcpp/FunctionCall.h>


// Explicitly stated dependencies in stdlib.h
// Non-standard, writes at most n bytes to dest with printf formatting
#if defined(CLCPP_PLATFORM_WINDOWS)
	extern "C" int _snprintf(char* dest, unsigned int n, const char* fmt, ...);
	#define snprintf _snprintf
#else
	extern "C" int snprintf(char* dest, unsigned int n, const char* fmt, ...);
#endif


static void SetupTypeDispatchLUT();


namespace
{
	// ----------------------------------------------------------------------------------------------------
	// Perfect hash based load/save function dispatching
	// ----------------------------------------------------------------------------------------------------


	typedef void (*SaveNumberFunc)(clutl::WriteBuffer&, const char*, unsigned int flags);
	typedef void (*LoadIntegerFunc)(char*, clcpp::int64);
	typedef void (*LoadDecimalFunc)(char*, double);


	struct TypeDispatch
	{
		TypeDispatch()
			: valid(false)
			, save_number(0)
			, load_integer(0)
			, load_decimal(0)
		{
		}

		bool valid;
		SaveNumberFunc save_number;
		LoadIntegerFunc load_integer;
		LoadDecimalFunc load_decimal;
	};


	// A save function lookup table for all supported C++ types
	static const int g_TypeDispatchMod = 47;
	TypeDispatch g_TypeDispatchLUT[g_TypeDispatchMod];
	bool g_TypeDispatchLUTReady = false;


	// For the given data set of basic type name hashes, this combines to make
	// a perfect hash function with no collisions, allowing quick indexed lookup.
	unsigned int GetTypeDispatchIndex(unsigned int hash)
	{
		return hash % g_TypeDispatchMod;
	}
	unsigned int GetTypeDispatchIndex(const char* type_name)
	{
		return GetTypeDispatchIndex(clcpp::internal::HashNameString(type_name));
	}


	void AddTypeDispatch(const char* type_name, SaveNumberFunc save_func, LoadIntegerFunc loadi_func, LoadDecimalFunc loadd_func)
	{
		// Ensure there are no collisions before adding the functions
		unsigned index = GetTypeDispatchIndex(type_name);
		clcpp::internal::Assert(g_TypeDispatchLUT[index].valid == false && "Lookup table index collision");
		g_TypeDispatchLUT[index].valid = true;
		g_TypeDispatchLUT[index].save_number = save_func;
		g_TypeDispatchLUT[index].load_integer = loadi_func;
		g_TypeDispatchLUT[index].load_decimal = loadd_func;
	}


	// ----------------------------------------------------------------------------------------------------
	// JSON Parser & reflection-based object construction
	// ----------------------------------------------------------------------------------------------------


	void ParserValue(clutl::JSONContext& ctx, clutl::JSONToken& t, char* object, const clcpp::Type* type, clcpp::Qualifier::Operator op, const clcpp::Field* field);
	void ParserObject(clutl::JSONContext& ctx, clutl::JSONToken& t, char* object, const clcpp::Type* type);


	clutl::JSONToken Expect(clutl::JSONContext& ctx, clutl::JSONToken& t, clutl::JSONTokenType type)
	{
		// Check the tokens match
		if (t.type != type)
		{
			ctx.SetError(clutl::JSONError::UNEXPECTED_TOKEN);
			return clutl::JSONToken();
		}

		// Look-ahead one token
		clutl::JSONToken old = t;
		t = LexerNextToken(ctx);
		return old;
	}


	void ParserString(const clutl::JSONToken& t, char* object, const clcpp::Type* type)
	{
		// Was there an error expecting a string?
		if (!t.IsValid())
			return;

		// With enum fields, lookup the enum constant by name and assign if it exists
		if (type && type->kind == clcpp::Primitive::KIND_ENUM)
		{
			const clcpp::Enum* enum_type = type->AsEnum();
			unsigned int constant_hash = clcpp::internal::HashData(t.val.string, t.length);
			const clcpp::EnumConstant* constant = clcpp::FindPrimitive(enum_type->constants, constant_hash);
			if (constant)
				*(int*)object = constant->value;
		}
	}


	template <typename TYPE>
	void LoadIntegerWithCast(char* dest, clcpp::int64 integer)
	{
		*(TYPE*)dest = (TYPE)integer;
	}
	void LoadIntegerBool(char* dest, clcpp::int64 integer)
	{
		*(bool*)dest = integer != 0;
	}


	template <typename TYPE>
	void LoadDecimalWithCast(char* dest, double decimal)
	{
		*(TYPE*)dest = (TYPE)decimal;
	}
	void LoadDecimalBool(char* dest, double decimal)
	{
		*(bool*)dest = decimal != 0;
	}


	void LoadInteger(clutl::JSONContext& ctx, clcpp::int64 integer, char* object, const clcpp::Type* type, clcpp::Qualifier::Operator op)
	{
		if (type == 0)
			return;

		if (op == clcpp::Qualifier::POINTER)
		{
			*(unsigned int*)object = (unsigned int)integer;
		}

		else
		{
			// Dispatch to the correct integer loader based on the field type
			unsigned int index = GetTypeDispatchIndex(type->name.hash);
			clcpp::internal::Assert(index < g_TypeDispatchMod && "Index is out of range");
			LoadIntegerFunc func = g_TypeDispatchLUT[index].load_integer;
			if (func)
				func(object, integer);
		}
	}


	void ParserInteger(clutl::JSONContext& ctx, const clutl::JSONToken& t, char* object, const clcpp::Type* type, clcpp::Qualifier::Operator op)
	{
		if (t.IsValid())
			LoadInteger(ctx, t.val.integer, object, type, op);
	}


	void ParserDecimal(const clutl::JSONToken& t, char* object, const clcpp::Type* type)
	{
		// Was there an error expecting a decimal?
		if (!t.IsValid())
			return;

		if (type)
		{
			// Dispatch to the correct decimal loader based on the field type
			unsigned int index = GetTypeDispatchIndex(type->name.hash);
			clcpp::internal::Assert(index < g_TypeDispatchMod && "Index is out of range");
			LoadDecimalFunc func = g_TypeDispatchLUT[index].load_decimal;
			if (func)
				func(object, t.val.decimal);
		}
	}


	int ParserElements(clutl::JSONContext& ctx, clutl::JSONToken& t, clcpp::WriteIterator* writer, const clcpp::Type* type, clcpp::Qualifier::Operator op)
	{
		// Expect a value first
		if (writer)
			ParserValue(ctx, t, (char*)writer->AddEmpty(), type, op, 0);
		else
			ParserValue(ctx, t, 0, 0, op, 0);

		if (t.type == clutl::JSON_TOKEN_COMMA)
		{
			t = LexerNextToken(ctx);
			return 1 + ParserElements(ctx, t, writer, type, op);
		}

		return 1;
	}


	void ParserArray(clutl::JSONContext& ctx, clutl::JSONToken& t, char* object, const clcpp::Type* type, const clcpp::Field* field)
	{
		if (!Expect(ctx, t, clutl::JSON_TOKEN_LBRACKET).IsValid())
			return;

		// Empty array?
		if (t.type == clutl::JSON_TOKEN_RBRACKET)
		{
			t = LexerNextToken(ctx);
			return;
		}

		clcpp::WriteIterator writer;
		if (field && field->ci)
		{
			// Fields are fixed array iterators
			writer.Initialise(field, object);
		}

		else if (type && type->ci)
		{
			// Do a pre-pass on the array to count the number of elements
			// Really not very efficient for big collections of large objects
			ctx.PushState(t);
			int array_count = ParserElements(ctx, t, 0, 0, clcpp::Qualifier::VALUE);
			ctx.PopState(t);

			// Template types are dynamic container iterators
			writer.Initialise(type->AsTemplateType(), object, array_count);
		}

		if (writer.IsInitialised())
			ParserElements(ctx, t, &writer, writer.m_ValueType, writer.m_ValueIsPtr ? clcpp::Qualifier::POINTER : clcpp::Qualifier::VALUE);
		else
			ParserElements(ctx, t, 0, 0, clcpp::Qualifier::VALUE);

		Expect(ctx, t, clutl::JSON_TOKEN_RBRACKET);
	}


	void ParserLiteralValue(clutl::JSONContext& ctx, const clutl::JSONToken& t, int integer, char* object, const clcpp::Type* type, clcpp::Qualifier::Operator op)
	{
		if (t.IsValid())
			LoadInteger(ctx, integer, object, type, op);
	}


	void ParserValue(clutl::JSONContext& ctx, clutl::JSONToken& t, char* object, const clcpp::Type* type, clcpp::Qualifier::Operator op, const clcpp::Field* field)
	{
		if (type && type->kind == clcpp::Primitive::KIND_CLASS)
		{
			const clcpp::Class* class_type = type->AsClass();

			// Does this class have a custom load function?
			if (class_type->flag_attributes & clcpp::FlagAttribute::CUSTOM_LOAD)
			{
				// Look it up
				static unsigned int hash = clcpp::internal::HashNameString("load_json");
				if (const clcpp::Attribute* attr = clcpp::FindPrimitive(class_type->attributes, hash))
				{
					const clcpp::PrimitiveAttribute* name_attr = attr->AsPrimitiveAttribute();

					// Call it and return immediately
					clcpp::CallFunction((clcpp::Function*)name_attr->primitive, clcpp::ByRef(t), object);
					t = LexerNextToken(ctx);
					return;
				}
			}
		}

		switch (t.type)
		{
		case clutl::JSON_TOKEN_STRING: return ParserString(Expect(ctx, t, clutl::JSON_TOKEN_STRING), object, type);
		case clutl::JSON_TOKEN_INTEGER: return ParserInteger(ctx, Expect(ctx, t, clutl::JSON_TOKEN_INTEGER), object, type, op);
		case clutl::JSON_TOKEN_DECIMAL: return ParserDecimal(Expect(ctx, t, clutl::JSON_TOKEN_DECIMAL), object, type);
		case clutl::JSON_TOKEN_LBRACE:
			{
				if (type)
					ParserObject(ctx, t, object, type);
				else
					ParserObject(ctx, t, 0, 0);

				Expect(ctx, t, clutl::JSON_TOKEN_RBRACE);
				break;
			}
		case clutl::JSON_TOKEN_LBRACKET: return ParserArray(ctx, t, object, type, field);
		case clutl::JSON_TOKEN_TRUE: return ParserLiteralValue(ctx, Expect(ctx, t, clutl::JSON_TOKEN_TRUE), 1, object, type, op);
		case clutl::JSON_TOKEN_FALSE: return ParserLiteralValue(ctx, Expect(ctx, t, clutl::JSON_TOKEN_FALSE), 0, object, type, op);
		case clutl::JSON_TOKEN_NULL: return ParserLiteralValue(ctx, Expect(ctx, t, clutl::JSON_TOKEN_NULL), 0, object, type, op);

		default:
			ctx.SetError(clutl::JSONError::UNEXPECTED_TOKEN);
			break;
		}
	}


	const clcpp::Field* FindFieldsRecursive(const clcpp::Type* type, unsigned int hash)
	{
		// Check fields if this is a class
		const clcpp::Field* field = 0;
		if (type->kind == clcpp::Primitive::KIND_CLASS)
			field = clcpp::FindPrimitive(type->AsClass()->fields, hash);

		if (field == 0)
		{
			// Search up through the inheritance hierarchy
			for (unsigned int i = 0; i < type->base_types.size; i++)
			{
				field = FindFieldsRecursive(type->base_types[i], hash);
				if (field)
					break;
			}
		}

		return field;
	}


	void ParserPair(clutl::JSONContext& ctx, clutl::JSONToken& t, char*& object, const clcpp::Type*& type)
	{
		// Get the field name
		clutl::JSONToken name = Expect(ctx, t, clutl::JSON_TOKEN_STRING);
		if (!name.IsValid())
			return;

		// Lookup the field in the parent class, if the type is class
		// We want to continue parsing even if there's a mismatch, to skip the invalid data
		const clcpp::Field* field = 0;
		if (type && type->kind == clcpp::Primitive::KIND_CLASS)
		{
			const clcpp::Class* class_type = type->AsClass();
			unsigned int field_hash = clcpp::internal::HashData(name.val.string, name.length);

			field = FindFieldsRecursive(class_type, field_hash);

			// Don't load values for transient fields
			if (field && (field->flag_attributes & clcpp::FlagAttribute::TRANSIENT))
				field = 0;
		}

		if (!Expect(ctx, t, clutl::JSON_TOKEN_COLON).IsValid())
			return;

		// Parse or skip the field if it's unknown
		if (field)
			ParserValue(ctx, t, object + field->offset, field->type, field->qualifier.op, field);
		else
			ParserValue(ctx, t, 0, 0, clcpp::Qualifier::VALUE, 0);
	}


	void ParserMembers(clutl::JSONContext& ctx, clutl::JSONToken& t, char* object, const clcpp::Type* type)
	{
		ParserPair(ctx, t, object, type);

		// Recurse, parsing more members in the list
		if (t.type == clutl::JSON_TOKEN_COMMA)
		{
			t = LexerNextToken(ctx);
			ParserMembers(ctx, t, object, type);
		}
	}


	void ParserObject(clutl::JSONContext& ctx, clutl::JSONToken& t, char* object, const clcpp::Type* type)
	{
		if (!Expect(ctx, t, clutl::JSON_TOKEN_LBRACE).IsValid())
			return;

		// Empty object?
		if (t.type == clutl::JSON_TOKEN_RBRACE)
		{
			//t = LexerNextToken(ctx);
			return;
		}

		ParserMembers(ctx, t, object, type);
		
		if (type && type->kind == clcpp::Primitive::KIND_CLASS)
		{
			const clcpp::Class* class_type = type->AsClass();

			// Run any attached post-load functions
			if (class_type->flag_attributes & clcpp::FlagAttribute::POST_LOAD)
			{
				static unsigned int hash = clcpp::internal::HashNameString("post_load");
				if (const clcpp::Attribute* attr = clcpp::FindPrimitive(class_type->attributes, hash))
				{
					const clcpp::PrimitiveAttribute* name_attr = attr->AsPrimitiveAttribute();
					if (name_attr->primitive != 0)
						clcpp::CallFunction((clcpp::Function*)name_attr->primitive, object);
				}
			}
		}
	}
}


clutl::JSONError clutl::LoadJSON(ReadBuffer& in, void* object, const clcpp::Type* type)
{
	SetupTypeDispatchLUT();
	clutl::JSONContext ctx(in);
	clutl::JSONToken t = LexerNextToken(ctx);
	ParserObject(ctx, t, (char*)object, type);
	return ctx.GetError();
}


clutl::JSONError clutl::LoadJSON(clutl::JSONContext& ctx, void* object, const clcpp::Field* field)
{
	SetupTypeDispatchLUT();
	clutl::JSONToken t = LexerNextToken(ctx);
	ParserValue(ctx, t, (char*)object, field->type, field->qualifier.op, field);
	return ctx.GetError();
}


namespace
{
	// ----------------------------------------------------------------------------------------------------
	// JSON text writer using reflected objects
	// ----------------------------------------------------------------------------------------------------


	void SaveObject(clutl::WriteBuffer& out, const char* object, const clcpp::Field* field, const clcpp::Type* type, clutl::IPtrSave* ptr_save, unsigned int flags);


	void SaveString(clutl::WriteBuffer& out, const char* start, const char* end)
	{
		out.WriteChar('\"');
		out.Write(start, end - start);
		out.WriteChar('\"');
	}


	void SaveString(clutl::WriteBuffer& out, const char* str)
	{
		out.WriteChar('\"');
		out.WriteStr(str);
		out.WriteChar('\"');
	}


	void SaveInteger(clutl::WriteBuffer& out, clcpp::int64 integer)
	{
		// Enough to store a 64-bit int
		static const int MAX_SZ = 20;
		char text[MAX_SZ];

		// Null terminate and start at the end
		text[MAX_SZ - 1] = 0;
		char* end = text + MAX_SZ - 1;
		char* tptr = end;

		// Get the absolute and record if the value is negative
		bool negative = false;
		if (integer < 0)
		{
			negative = true;
			integer = -integer;
		}

		// Loop through the value with radix 10
		do 
		{
			clcpp::int64 next_integer = integer / 10;
			*--tptr = char('0' + (integer - next_integer * 10));
			integer = next_integer;
		} while (integer);

		// Add negative prefix
		if (negative)
			*--tptr = '-';

		out.Write(tptr, end - tptr);
	}


	void SaveUnsignedInteger(clutl::WriteBuffer& out, clcpp::uint64 integer)
	{
		// Enough to store a 64-bit int + null
		static const int MAX_SZ = 21;
		char text[MAX_SZ];

		// Null terminate and start at the end
		text[MAX_SZ - 1] = 0;
		char* end = text + MAX_SZ - 1;
		char* tptr = end;

		// Loop through the value with radix 10
		do 
		{
			clcpp::uint64 next_integer = integer / 10;
			*--tptr = char('0' + (integer - next_integer * 10));
			integer = next_integer;
		} while (integer);

		out.Write(tptr, end - tptr);
	}


	void SaveHexInteger(clutl::WriteBuffer& out, clcpp::uint64 integer)
	{
		// Enough to store a 64-bit int + null
		static const int MAX_SZ = 21;
		char text[MAX_SZ];

		// Null terminate and start at the end
		text[MAX_SZ - 1] = 0;
		char* end = text + MAX_SZ - 1;
		char* tptr = end;

		// Loop through the value with radix 16
		do 
		{
			clcpp::uint64 next_integer = integer / 16;
			*--tptr = "0123456789ABCDEF"[integer - next_integer * 16];
			integer = next_integer;
		} while (integer);

		out.Write(tptr, end - tptr);
	}


	// Automate the process of de-referencing an object as its exact type so no
	// extra bytes are read incorrectly and values are correctly zero-extended.
	template <typename TYPE>
	void SaveIntegerWithCast(clutl::WriteBuffer& out, const char* object, unsigned int)
	{
		SaveInteger(out, *(TYPE*)object);
	}
	template <typename TYPE>
	void SaveUnsignedIntegerWithCast(clutl::WriteBuffer& out, const char* object, unsigned int)
	{
		SaveUnsignedInteger(out, *(TYPE*)object);
	}


	void SaveDecimal(clutl::WriteBuffer& out, double decimal, unsigned int flags)
	{
		if (flags & clutl::JSONFlags::EMIT_HEX_FLOATS)
		{
			// Use a specific prefix to inform the lexer to alias as a decimal
			out.WriteStr("0d");
			SaveHexInteger(out, (clcpp::uint64&)decimal);
			return;
		}

		// Serialise full float rep
		char float_buffer[512];
		int count = snprintf(float_buffer, sizeof(float_buffer), "%f", decimal);
		out.Write(float_buffer, count);
	}


	void SaveDouble(clutl::WriteBuffer& out, const char* object, unsigned int flags)
	{
		SaveDecimal(out, *(double*)object, flags);
	}
	void SaveFloat(clutl::WriteBuffer& out, const char* object, unsigned int flags)
	{
		SaveDecimal(out, *(float*)object, flags);
	}


	void SaveType(clutl::WriteBuffer& out, const char* object, const clcpp::Type* type, unsigned int flags)
	{
		unsigned int index = GetTypeDispatchIndex(type->name.hash);
		clcpp::internal::Assert(index < g_TypeDispatchMod && "Index is out of range");
		SaveNumberFunc func = g_TypeDispatchLUT[index].save_number;
		clcpp::internal::Assert(func && "No save function for type");
		func(out, object, flags);
	}


	void SaveEnum(clutl::WriteBuffer& out, const char* object, const clcpp::Enum* enum_type)
	{
		// Do a linear search for an enum with a matching value
		int value = *(int*)object;
		const char* enum_name = "clReflect_JSON_EnumValueNotFound";
		for (unsigned int i = 0; i < enum_type->constants.size; i++)
		{
			if (enum_type->constants[i]->value == value)
			{
				enum_name = enum_type->constants[i]->name.text;
				break;
			}
		}

		// Write the enum name as the value
		SaveString(out, enum_name);
	}


	void SavePtr(clutl::WriteBuffer& out, const void* object, clutl::IPtrSave* ptr_save, unsigned int flags)
	{
		// Get the filtered pointer from the caller
		void* ptr = *(void**)object;
		unsigned int hash = ptr_save->SavePtr(ptr);

		if ((flags & clutl::JSONFlags::EMIT_HEX_POINTERS) != 0)
		{
			out.WriteStr("0x");
			SaveHexInteger(out, hash);
		}
		else
		{
			SaveUnsignedInteger(out, hash);
		}
	}


	void SaveContainer(clutl::WriteBuffer& out, clcpp::ReadIterator& reader, const clcpp::Field* field, clutl::IPtrSave* ptr_save, unsigned int flags)
	{
		// TODO: If the iterator has a key, save a dictionary instead.
		// TODO: The reader knows its type and if its a pointer for all entries. Can early out on unwanted pointer saves, etc.

		out.WriteChar('[');

		// Save comma-separated objects
		bool written = false;
		for (unsigned int i = 0; i < reader.m_Count; i++)
		{
			clcpp::ContainerKeyValue kv = reader.GetKeyValue();

			if (written)
				out.WriteChar(',');

			if (reader.m_ValueIsPtr)
			{
				// Ask the user if they want to save this pointer
				void* ptr = *(void**)kv.value;
				if (ptr_save == 0 || !ptr_save->CanSavePtr(ptr, field, reader.m_ValueType))
					continue;

				SavePtr(out, kv.value, ptr_save, flags);
			}
			else
			{
				SaveObject(out, (char*)kv.value, field, reader.m_ValueType, ptr_save, flags);
			}

			reader.MoveNext();
			written = true;
		}

		out.WriteChar(']');
	}


	void SaveFieldArray(clutl::WriteBuffer& out, const char* object, const clcpp::Field* field, clutl::IPtrSave* ptr_save, unsigned int flags)
	{
		// Construct a read iterator and leave early if there are no elements
		clcpp::ReadIterator reader(field, object);
		if (reader.m_Count == 0)
		{
			out.WriteStr("[]");
			return;
		}

		SaveContainer(out, reader, field, ptr_save, flags);
	}


	inline void NewLine(clutl::WriteBuffer& out, unsigned int flags)
	{
		if (flags & clutl::JSONFlags::FORMAT_OUTPUT)
		{
			out.WriteChar('\n');

			// Open the next new line with tabs
			for (unsigned int i = 0; i < (flags & clutl::JSONFlags::INDENT_MASK); i++)
				out.WriteChar('\t');
		}
	}


	inline void OpenScope(clutl::WriteBuffer& out, unsigned int& flags)
	{
		if (flags & clutl::JSONFlags::FORMAT_OUTPUT)
		{
			NewLine(out, flags);
			out.WriteChar('{');
			
			// Increment indent level
			int indent_level = flags & clutl::JSONFlags::INDENT_MASK;
			flags &= ~clutl::JSONFlags::INDENT_MASK;
			flags |= ((indent_level + 1) & clutl::JSONFlags::INDENT_MASK);

			NewLine(out, flags);
		}
		else
		{
			out.WriteChar('{');
		}
	}


	inline void CloseScope(clutl::WriteBuffer& out, unsigned int& flags)
	{
		if (flags & clutl::JSONFlags::FORMAT_OUTPUT)
		{
			// Decrement indent level
			int indent_level = flags & clutl::JSONFlags::INDENT_MASK;
			flags &= ~clutl::JSONFlags::INDENT_MASK;
			flags |= ((indent_level - 1) & clutl::JSONFlags::INDENT_MASK);

			NewLine(out, flags);
			out.WriteChar('}');
			NewLine(out, flags);
		}
		else
		{
			out.WriteChar('}');
		}
	}


	void SaveFieldObject(clutl::WriteBuffer& out, const char* object, const clcpp::Field* field, clutl::IPtrSave* ptr_save, unsigned int& flags)
	{
		if (field->ci != 0)
			SaveFieldArray(out, object, field, ptr_save, flags);
		else if (field->qualifier.op == clcpp::Qualifier::POINTER)
			SavePtr(out, object, ptr_save, flags);
		else
			SaveObject(out, object, field, field->type, ptr_save, flags);
	}


	void SaveClassField(clutl::WriteBuffer& out, const char* object, const clcpp::Field* field, clutl::IPtrSave* ptr_save, unsigned int& flags, bool& field_written)
	{
		if (field->qualifier.op == clcpp::Qualifier::POINTER)
		{
			// Ask the caller if they want to save this pointer
			void* ptr = *(void**)(object + field->offset);
			if (ptr_save == 0 || !ptr_save->CanSavePtr(ptr, field, field->type))
				return;
		}

		// Comma separator for multiple fields
		if (field_written)
		{
			out.WriteChar(',');
			NewLine(out, flags);
		}

		// Write the field name and object
		SaveString(out, field->name.text);
		out.WriteChar(':');
		SaveFieldObject(out, object + field->offset, field, ptr_save, flags);
		field_written = true;
	}


	void SaveClassFields(clutl::WriteBuffer& out, const char* object, const clcpp::Class* class_type, clutl::IPtrSave* ptr_save, unsigned int& flags, bool& field_written)
	{
		const clcpp::CArray<const clcpp::Field*>& fields = class_type->fields;
		unsigned int nb_fields = fields.size;

		if ((flags & clutl::JSONFlags::SORT_CLASS_FIELDS_BY_OFFSET) != 0)
		{
			int last_field_offset = -1;

			// Save fields in offset-sorted order
			// This is a brute-force O(n^2) implementation as I don't want to allocate any heap memory or stack memory
			// with an upper bound. As the source array is also const and needs to be used elsewhere, sorting in-place
			// is also not an option.
			for (unsigned int i = 0; i < nb_fields; i++)
			{
				int lowest_field_offset = (1 << 31) - 1;
				int lowest_field_index = -1;

				// Search for the next field with the lowest offset
				for (unsigned int j = 0; j < nb_fields; j++)
				{
					// Skip transient fields
					const clcpp::Field* field = fields[j];
					if (field->flag_attributes & clcpp::FlagAttribute::TRANSIENT)
						continue;

					if (field->offset > last_field_offset && field->offset < lowest_field_offset)
					{
						lowest_field_offset = field->offset;
						lowest_field_index = j;
					}
				}

				// Use of transient implies not all fields may be serialised
				if (lowest_field_index != -1)
				{
					const clcpp::Field* field = fields[lowest_field_index];
					SaveClassField(out, object, field, ptr_save, flags, field_written);
					last_field_offset = lowest_field_offset;
				}
			}
		}

		else
		{
			// Save fields in array order
			for (unsigned int i = 0; i < nb_fields; i++)
			{
				// Skip transient fields
				const clcpp::Field* field = fields[i];
				if (field->flag_attributes & clcpp::FlagAttribute::TRANSIENT)
					continue;

				SaveClassField(out, object, field, ptr_save, flags, field_written);
			}
		}
	}


	void SaveClass(clutl::WriteBuffer& out, const char* object, const clcpp::Type* type, clutl::IPtrSave* ptr_save, unsigned int& flags, bool& field_written)
	{
		// Save body of the class
		if (type->kind == clcpp::Primitive::KIND_CLASS)
			SaveClassFields(out, object, type->AsClass(), ptr_save, flags, field_written);

		// Recurse into base types
		for (unsigned int i = 0; i < type->base_types.size; i++)
		{
			const clcpp::Type* base_type = type->base_types[i];
			SaveClass(out, object, base_type, ptr_save, flags, field_written);
		}
	}


	void SaveClass(clutl::WriteBuffer& out, const char* object, const clcpp::Class* class_type, clutl::IPtrSave* ptr_save, unsigned int flags)
	{
		// Is there a custom loading function for this class?
		if (class_type->flag_attributes & clcpp::FlagAttribute::CUSTOM_SAVE)
		{
			// Look it up
			static unsigned int hash = clcpp::internal::HashNameString("save_json");
			if (const clcpp::Attribute* attr = clcpp::FindPrimitive(class_type->attributes, hash))
			{
				const clcpp::PrimitiveAttribute* name_attr = attr->AsPrimitiveAttribute();

				// Call the function to generate an output token
				clutl::JSONToken token;
				clcpp::CallFunction((clcpp::Function*)name_attr->primitive, clcpp::ByRef(token), object);

				// Serialise appropriately
				switch (token.type)
				{
				case clutl::JSON_TOKEN_STRING:
					SaveString(out, token.val.string, token.val.string + token.length);
					break;
				case clutl::JSON_TOKEN_INTEGER:
					SaveInteger(out, token.val.integer);
					break;
				case clutl::JSON_TOKEN_DECIMAL:
					SaveDecimal(out, token.val.decimal, flags);
					break;
				default:
					clcpp::internal::Assert(false && "Invalid token output type");
				}

				return;
			}
		}

		// Call any attached pre-save function
		if (class_type->flag_attributes & clcpp::FlagAttribute::PRE_SAVE)
		{
			static unsigned int hash = clcpp::internal::HashNameString("pre_save");
			if (const clcpp::Attribute* attr = clcpp::FindPrimitive(class_type->attributes, hash))
			{
				const clcpp::PrimitiveAttribute* name_attr = attr->AsPrimitiveAttribute();
				if (name_attr->primitive != 0)
					clcpp::CallFunction((clcpp::Function*)name_attr->primitive, object);
			}
		}

		bool field_written = false;
		OpenScope(out, flags);
		SaveClass(out, object, class_type, ptr_save, flags, field_written);
		CloseScope(out, flags);
	}


	void SaveTemplateType(clutl::WriteBuffer& out, const char* object, const clcpp::Field* field, const clcpp::TemplateType* template_type, clutl::IPtrSave* ptr_save, unsigned int flags)
	{
		// Construct a read iterator and leave early if there are no elements
		clcpp::ReadIterator reader(template_type, object);
		if (reader.m_Count == 0)
		{
			out.WriteStr("[]");
			return;
		}

		SaveContainer(out, reader, field, ptr_save, flags);
	}


	void SaveObject(clutl::WriteBuffer& out, const char* object, const clcpp::Field* field, const clcpp::Type* type, clutl::IPtrSave* ptr_save, unsigned int flags)
	{
		// Dispatch to a save function based on kind
		switch (type->kind)
		{
		case clcpp::Primitive::KIND_TYPE:
			SaveType(out, object, type, flags);
			break;

		case clcpp::Primitive::KIND_ENUM:
			SaveEnum(out, object, type->AsEnum());
			break;

		case clcpp::Primitive::KIND_CLASS:
			SaveClass(out, object, type->AsClass(), ptr_save, flags);
			break;

		case clcpp::Primitive::KIND_TEMPLATE_TYPE:
			SaveTemplateType(out, object, field, type->AsTemplateType(), ptr_save, flags);
			break;

		default:
			clcpp::internal::Assert(false && "Invalid primitive kind for type");
		}
	}
}


void clutl::SaveJSON(WriteBuffer& out, const void* object, const clcpp::Type* type, IPtrSave* ptr_save, unsigned int flags)
{
	SetupTypeDispatchLUT();
	SaveObject(out, (char*)object, 0, type, ptr_save, flags);
}


void clutl::SaveJSON(WriteBuffer& out, const void* object, const clcpp::Field* field, IPtrSave* ptr_save, unsigned int flags)
{
	SetupTypeDispatchLUT();
	SaveFieldObject(out, (char*)object, field, ptr_save, flags);
}


static void SetupTypeDispatchLUT()
{
	if (!g_TypeDispatchLUTReady)
	{
		// Add all integers
		AddTypeDispatch("bool", SaveIntegerWithCast<bool>, LoadIntegerBool, LoadDecimalBool);
		AddTypeDispatch("char", SaveIntegerWithCast<char>, LoadIntegerWithCast<char>, LoadDecimalWithCast<char>);
		AddTypeDispatch("wchar_t", SaveUnsignedIntegerWithCast<wchar_t>, LoadIntegerWithCast<wchar_t>, LoadDecimalWithCast<wchar_t>);
		AddTypeDispatch("unsigned char", SaveUnsignedIntegerWithCast<unsigned char>, LoadIntegerWithCast<unsigned char>, LoadDecimalWithCast<unsigned char>);
		AddTypeDispatch("short", SaveIntegerWithCast<short>, LoadIntegerWithCast<short>, LoadDecimalWithCast<short>);
		AddTypeDispatch("unsigned short", SaveUnsignedIntegerWithCast<unsigned short>, LoadIntegerWithCast<unsigned short>, LoadDecimalWithCast<unsigned short>);
		AddTypeDispatch("int", SaveIntegerWithCast<int>, LoadIntegerWithCast<int>, LoadDecimalWithCast<int>);
		AddTypeDispatch("unsigned int", SaveUnsignedIntegerWithCast<unsigned int>, LoadIntegerWithCast<unsigned int>, LoadDecimalWithCast<unsigned int>);
		AddTypeDispatch("long", SaveIntegerWithCast<long>, LoadIntegerWithCast<long>, LoadDecimalWithCast<long>);
		AddTypeDispatch("unsigned long", SaveUnsignedIntegerWithCast<unsigned long>, LoadIntegerWithCast<unsigned long>, LoadDecimalWithCast<unsigned long>);
		AddTypeDispatch("long long", SaveIntegerWithCast<long long>, LoadIntegerWithCast<long long>, LoadDecimalWithCast<long long>);
		AddTypeDispatch("unsigned long long", SaveUnsignedIntegerWithCast<unsigned long long>, LoadIntegerWithCast<unsigned long long>, LoadDecimalWithCast<unsigned long long>);

		// Add all decimals
		AddTypeDispatch("float", SaveFloat, LoadIntegerWithCast<float>, LoadDecimalWithCast<float>);
		AddTypeDispatch("double", SaveDouble, LoadIntegerWithCast<double>, LoadDecimalWithCast<double>);

		g_TypeDispatchLUTReady = true;
	}
}


