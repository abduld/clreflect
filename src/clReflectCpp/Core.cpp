
#include <crcpp/Core.h>


namespace
{
	// Rotate left - some compilers can optimise this to a single rotate!
	unsigned int rotl(unsigned int v, unsigned int bits)
	{
		return (v << bits) | (v >> (32 - bits));
	}


	unsigned int fmix(unsigned int h)
	{
		// Finalisation mix - force all bits of a hash block to avalanche
		h ^= h >> 16;
		h *= 0x85ebca6b;
		h ^= h >> 13;
		h *= 0xc2b2ae35;
		h ^= h >> 16;
		return h;
	}


	//
	// Austin Appleby's MurmurHash 3: http://code.google.com/p/smhasher
	//
	unsigned int MurmurHash3(const void* key, int len, unsigned int seed)
	{
		const unsigned char* data = (const unsigned char*)key;
		int nb_blocks = len / 4;

		unsigned int h1 = seed;
		unsigned int c1 = 0xcc9e2d51;
		unsigned int c2 = 0x1b873593;

		// Body
		const unsigned int* blocks = (const unsigned int*)(data + nb_blocks * 4);
		for (int i = -nb_blocks; i; i++)
		{
			unsigned int k1 = blocks[i];

			k1 *= c1;
			k1 = rotl(k1, 15);
			k1 *= c2;

			h1 ^= k1;
			h1 = rotl(h1, 13);
			h1 = h1 * 5 + 0xe6546b64;
		}

		// Tail
		const unsigned char* tail = (const unsigned char*)(data + nb_blocks * 4);
		unsigned int k1 = 0;
		switch (len & 3)
		{
		case (3): k1 ^= tail[2] << 16;
		case (2): k1 ^= tail[1] << 8;
		case (1): k1 ^= tail[0];
			k1 *= c1;
			k1 = rotl(k1, 15);
			k1 *= c2;
			h1 ^= k1;
		}

		// Finalisation
		h1 ^= len;
		h1 = fmix(h1);
		return h1;
	}


	int strlen(const char* str)
	{
		int len = 0;
		while (*str++)
			len++;
		return len;
	}
}


unsigned int crcpp::internal::HashNameString(const char* name_string)
{
	return MurmurHash3(name_string, strlen(name_string), 0);
}


unsigned int crcpp::internal::MixHashes(unsigned int a, unsigned int b)
{
	return MurmurHash3(&b, sizeof(unsigned int), a);
}