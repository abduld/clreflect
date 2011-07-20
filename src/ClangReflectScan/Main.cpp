
#include "ClangFrontend.h"
#include "ASTConsumer.h"
#include "ReflectionSpecs.h"

#include "ClangReflectCore/Arguments.h"
#include "ClangReflectCore/Logging.h"
#include "ClangReflectCore/Database.h"
#include "ClangReflectCore/DatabaseTextSerialiser.h"
#include "ClangReflectCore/DatabaseBinarySerialiser.h"


namespace
{
	bool FileExists(const char* filename)
	{
		// For now, just try to open the file
		FILE* fp = fopen(filename, "r");
		if (fp == 0)
		{
			return false;
		}
		fclose(fp);
		return true;
	}

	
	bool EndsWith(const std::string& str, const std::string& end)
	{
		return str.rfind(end) == str.length() - end.length();
	}


	void PrintIncludedHeaders(const ClangASTParser& ast_parser, const char* input_filename)
	{
		std::vector<std::string> header_files;
		ast_parser.GetIncludedFiles(header_files);

		// Print to output, noting that the source file will also be in the list
		for (size_t i = 0; i < header_files.size(); i++)
		{
			if (header_files[i] != input_filename)
			{
				LOG(main, INFO, "Included: %s\n", header_files[i].c_str());
			}
		}
	}


	void WriteDatabase(const crdb::Database& db, const std::string& filename)
	{
		if (EndsWith(filename, ".csv"))
		{
			crdb::WriteTextDatabase(filename.c_str(), db);
		}
		else
		{
			crdb::WriteBinaryDatabase(filename.c_str(), db);
		}
	}


	void TestDBReadWrite(const crdb::Database& db)
	{
		crdb::WriteTextDatabase("output.csv", db);
		crdb::WriteBinaryDatabase("output.bin", db);

		crdb::Database indb_text;
		crdb::ReadTextDatabase("output.csv", indb_text);
		crdb::WriteTextDatabase("output2.csv", indb_text);

		crdb::Database indb_bin;
		crdb::ReadBinaryDatabase("output.bin", indb_bin);
		crdb::WriteBinaryDatabase("output2.bin", indb_bin);
	}
}


int main(int argc, const char* argv[])
{
	LOG_TO_STDOUT(main, ALL);

	// Leave early if there aren't enough arguments
	Arguments args(argc, argv);
	if (args.Count() < 2)
	{
		LOG(main, ERROR, "Not enough arguments\n");
		return 1;
	}

	// Does the input file exist?
	const char* input_filename = argv[1];
	if (!FileExists(input_filename))
	{
		LOG(main, ERROR, "Couldn't find the input file %s\n", input_filename);
		return 1;
	}

	// Parse the AST
	ClangHost clang_host;
	ClangASTParser ast_parser(clang_host);
	ast_parser.ParseAST(input_filename);

	// Gather reflection specs for the translation unit
	clang::ASTContext& ast_context = ast_parser.GetASTContext();
	std::string spec_log = args.GetProperty("-spec_log");
	ReflectionSpecs reflection_specs(args.Have("-reflect_specs_all"), spec_log);
	reflection_specs.Gather(ast_context.getTranslationUnitDecl());

	// On the second pass, build the reflection database
	crdb::Database db;
	db.AddBaseTypePrimitives();
	std::string ast_log = args.GetProperty("-ast_log");
	ASTConsumer ast_consumer(ast_context, db, reflection_specs, ast_log);
	ast_consumer.WalkTranlationUnit(ast_context.getTranslationUnitDecl());

	// Gather included header files if requested
	if (args.Have("-output_headers"))
	{
		PrintIncludedHeaders(ast_parser, input_filename);
	}

	// Write to a text/binary database depending upon extension
	std::string output = args.GetProperty("-output");
	if (output != "")
	{
		WriteDatabase(db, output);
	}

	if (args.Have("-test_db"))
	{
		TestDBReadWrite(db);
	}

	return 0;
}