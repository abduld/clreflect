
#include "ClangReflectCore/Database.h"


class ReflectionSpecs;


namespace clang
{
	class ASTContext;
	class ASTRecordLayout;
	class NamedDecl;
	class TranslationUnitDecl;
}


class ASTConsumer
{
public:
	ASTConsumer(clang::ASTContext& context, crdb::Database& db, const ReflectionSpecs& rspecs, const std::string& ast_log);

	void WalkTranlationUnit(clang::TranslationUnitDecl* tu_decl);

private:
	void AddDecl(clang::NamedDecl* decl, const crdb::Name& parent_name, const clang::ASTRecordLayout* layout);
	void AddNamespaceDecl(clang::NamedDecl* decl, const crdb::Name& name, const crdb::Name& parent_name);
	void AddClassDecl(clang::NamedDecl* decl, const crdb::Name& name, const crdb::Name& parent_name);
	void AddEnumDecl(clang::NamedDecl* decl, const crdb::Name& name, const crdb::Name& parent_name);
	void AddFunctionDecl(clang::NamedDecl* decl, const crdb::Name& name, const crdb::Name& parent_name);
	void AddMethodDecl(clang::NamedDecl* decl, const crdb::Name& name, const crdb::Name& parent_name);
	void AddFieldDecl(clang::NamedDecl* decl, const crdb::Name& name, const crdb::Name& parent_name, const clang::ASTRecordLayout* layout);
	void AddContainedDecls(clang::NamedDecl* decl, const crdb::Name& parent_name, const clang::ASTRecordLayout* layout);

	crdb::Database& m_DB;

	clang::ASTContext& m_ASTContext;

	const ReflectionSpecs& m_ReflectionSpecs;
};
