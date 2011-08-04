
#include <crcpp\Database.h>
#include "DatabaseLoader.h"


const crcpp::Primitive* crcpp::FindPrimitive(const CArray<const Primitive*>& primitives, Name name)
{
	// TODO: Return multiple entries

	int first = 0;
	int last = primitives.size() - 1;

	// Binary search
	while (first < last)
	{
		// Identify the mid point
		int mid = (first + last) / 2;
		unsigned int compare_hash = primitives[mid]->name.hash;

		if (name.hash > compare_hash)
		{
			// Shift search to local upper half
			first = mid + 1;
		}
		else if (name.hash < compare_hash)
		{
			// Shift search to local lower half
			last = mid - 1;
		}
		else
		{
			// Exact match found
			return primitives[mid];
		}
	}

	return 0;
}


crcpp::Database::Database()
	: m_DatabaseMem(0)
{
}


crcpp::Database::~Database()
{
	delete (char*)m_DatabaseMem;
}


bool crcpp::Database::Load(IFile* file)
{
	m_DatabaseMem = LoadMemoryMappedDatabase(file);
	return m_DatabaseMem != 0;
}
