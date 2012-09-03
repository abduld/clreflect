
clReflect v0.4 pre-release, C++ Reflection using clang
======================================================

Installation Instructions
-------------------------

The executables in the "bin" directory can be run directly but require the [MSVC 2010 x86 redistributables](http://www.microsoft.com/en-us/download/details.aspx?id=5555).

Quick Tour
----------

[clReflectTest](https://bitbucket.org/dwilliamson/clreflect/src/tip/src/clReflectTest) is an up-to-date test of the clReflect library, showing how to build a database and load it at runtime.

Basic build steps are:

# Use "bin\clscan" to parse your C++ files and output a readable database of type information.
# Use "bin\clmerge" to merge the various output files from clscan into one database file.
# Use "bin\clexport" to convert the merged database to a memory-mapped, C++ loadable/queryable database.


Use "bin\clscan" to parse your C++ files and output a readable database of type information:

	clscan.exe file_a.cpp -output file_a.csv -ast_log file_a_astlog.txt -spec_log file_a_speclog.txt -i "include_path"

Use "bin\clmerge" to merge the output of many C++ files into one readable database of type information:

	clmerge.exe module.csv file_a.csv file_b.csv file_c.csv ...

Use "bin\clexport" to convert the readable database to a memory-mapped, C++ loadable/queryable database:

	clexport.exe module.csv -cpp module.cppbin -cpp_log module_cpplog.txt -map module.map

Some points:

	* Without a reference to the mapfile generated by your executable/DLL (-map), function addresses won't be exported
	  with your database.
	* Only primitives specified by clcpp_reflect will be included in the output databases.
	* Pay attention to all reported warnings and be sure to inspect all output logs if you suspect there is a problem.
