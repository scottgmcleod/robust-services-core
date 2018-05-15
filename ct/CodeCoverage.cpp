//==============================================================================
//
//  CodeCoverage.cpp
//
//  Copyright (C) 2017  Greg Utas
//
//  This file is part of the Robust Services Core (RSC).
//
//  RSC is free software: you can redistribute it and/or modify it under the
//  terms of the GNU General Public License as published by the Free Software
//  Foundation, either version 3 of the License, or (at your option) any later
//  version.
//
//  RSC is distributed in the hope that it will be useful, but WITHOUT ANY
//  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
//  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
//  details.
//
//  You should have received a copy of the GNU General Public License along
//  with RSC.  If not, see <http://www.gnu.org/licenses/>.
//
#include "CodeCoverage.h"
#include <cctype>
#include <cstdio>
#include <iomanip>
#include <ios>
#include <istream>
#include <sstream>
#include <vector>
#include "Algorithms.h"
#include "CxxString.h"
#include "Debug.h"
#include "Element.h"
#include "Formatters.h"
#include "NbCliParms.h"
#include "SysFile.h"

using namespace NodeBase;
using std::ostream;
using std::string;

//------------------------------------------------------------------------------

namespace CodeTools
{
fn_name CodeCoverage_ctor = "CodeCoverage.ctor";

CodeCoverage::CodeCoverage()
{
   Debug::ft(CodeCoverage_ctor);
}

//------------------------------------------------------------------------------

fn_name CodeCoverage_AddFunc = "CodeCoverage.AddFunc";

bool CodeCoverage::AddFunc(const string& fn, uint32_t hash, const string& ns)
{
   Debug::ft(CodeCoverage_AddFunc);

   auto name = Mangle(fn);
   auto iter = currFuncs_.find(name);

   if(iter != currFuncs_.cend())
   {
      return (iter->second.ns == ns);
   }

   auto info = FuncInfo(ns, hash);
   auto result = currFuncs_.insert(FuncData(name, info));
   return result.second;
}

//------------------------------------------------------------------------------

fn_name CodeCoverage_AddTest = "CodeCoverage.AddTest";

bool CodeCoverage::AddTest
   (const std::string& test, bool found, const std::string& dir)
{
   Debug::ft(CodeCoverage_AddTest);

   //  If the testcase's script was found, create a string containing its
   //  contents and hash the string.  Remove any previous entry for the
   //  testcase before adding the testcase name and its hash value.
   //
   uint32_t hash = UNHASHED;

   if(found)
   {
      auto file = dir + PATH_SEPARATOR + test + ".txt";
      auto stream = SysFile::CreateIstream(file.c_str());
      if(stream == nullptr) return false;

      string contents;
      string input;

      while(stream->peek() != EOF)
      {
         std::getline(*stream, input);
         contents += input;
      }

      hash = stringHash(contents.c_str());
   }

   auto prev = currTests_.find(test);
   if(prev != currTests_.cend()) currTests_.erase(prev);
   auto info = TestInfo(hash);
   currTests_.insert(TestData(test, info));
   return true;
}

//------------------------------------------------------------------------------

fn_name CodeCoverage_Build = "CodeCoverage.Build";

word CodeCoverage::Build(string& expl)
{
   Debug::ft(CodeCoverage_Build);

   if(currFuncs_.empty())
   {
      expl = "Database is empty.  Run >check first.";
      return -1;
   }

   //  Find all *.funcs.txt files in the output directory and all *.txt
   //  files in the input directory.  When the same file name appears in
   //  both directories, add the testcase and its functions to the current
   //  database.
   //
   auto errors = 0;
   auto outdir = Element::OutputPath();
   auto indir = Element::InputPath();
   std::set< string > traces;
   std::set< string > tests;

   if(!FindFiles(outdir.c_str(), ".funcs.txt", traces))
   {
      expl = "Could not open directory " + outdir;
      return -1;
   }

   if(!FindFiles(indir.c_str(), ".txt", tests))
   {
      expl = "Could not open directory " + indir;
      return -1;
   }

   for(auto trace = traces.cbegin(); trace != traces.cend(); ++trace)
   {
      auto test = *trace;
      auto found = (tests.find(test) != tests.cend());

      if(!AddTest(test, found, indir))
      {
         ++errors;
         continue;
      }

      auto file = outdir + PATH_SEPARATOR + *trace + ".funcs.txt";
      auto stream = SysFile::CreateIstream(file.c_str());
      if(stream == nullptr)
      {
         ++errors;
         continue;
      }

      //  Extract the function names from STREAM.  They begin on the fourth
      //  line, and each one is preceded by two integers.
      //
      string input;
      string str;
      int discard = -4;

      while(stream->peek() != EOF)
      {
         std::getline(*stream, input);
         if(discard++ < 0) continue;

         str = GetString(input);
         if(str.empty() || !isdigit(str.front())) break;
         str = GetString(input);
         if(str.empty() || !isdigit(str.front())) break;
         str = GetString(input);
         if(str.empty()) break;

         //  If INPUT isn't empty, append it to STR.  The function name
         //  contains an embedded space (and might have more of them).
         //  Mangle replace spaces with "BLANK" to simplify Import.
         //
         if(!input.empty())
         {
            str += Mangle(input);
            input.clear();
         }

         auto func = currFuncs_.find(str);
         if(func == currFuncs_.cend())
         {
            //  This function was not in the database.  This occurs for a
            //  function template or function in a class template.  Add the
            //  function, assigning it a hash value that is easily noticed.
            //
            auto info = FuncInfo(UNHASHED);
            auto result = currFuncs_.insert(FuncData(str, info));
            func = result.first;
         }

         func->second.tests.insert(test);
      }
   }

   if(errors == 0)
      expl = SuccessExpl;
   else
      expl = "Errors: " + std::to_string(errors);
   return 0;
}

//------------------------------------------------------------------------------

string CodeCoverage::Demangle(const string& s)
{
   string result(s);

   for(auto i = 0; i < result.size(); ++i)
   {
      if(result[i] == BLANK) result[i] = SPACE;
   }

   return result;
}

//------------------------------------------------------------------------------

fn_name CodeCoverage_Diff = "CodeCoverage.Diff";

word CodeCoverage::Diff(string& expl) const
{
   Debug::ft(CodeCoverage_Diff);

   if(prevFuncs_.empty())
   {
      expl = "Database is empty.  Run >coverage load first.";
      return -1;
   }

   if(currTests_.empty())
   {
      expl = "No testcases in database.  Run >coverage build first.";
      return -1;
   }

   std::ostringstream report;
   report << "Added functions: ";
   auto found = false;

   for(auto c = currFuncs_.cbegin(); c != currFuncs_.cend(); ++c)
   {
      if(prevFuncs_.find(c->first) == prevFuncs_.cend())
      {
         report << CRLF << spaces(2) << c->first;
         found = true;
      }
   }

   if(!found) report << "none";
   report << CRLF << "Changed functions: ";
   found = false;

   for(auto c = currFuncs_.cbegin(); c != currFuncs_.cend(); ++c)
   {
      auto p = prevFuncs_.find(c->first);

      if((p != prevFuncs_.cend()) && (c->second.hash != p->second.hash))
      {
         report << CRLF << spaces(2) << c->first;
         found = true;
      }
   }

   if(!found) report << "none";
   report << CRLF << "Deleted functions: ";
   found = false;

   for(auto p = prevFuncs_.cbegin(); p!= prevFuncs_.cend(); ++p)
   {
      if((p->second.hash != UNHASHED) &&
         (currFuncs_.find(p->first) == currFuncs_.cend()))
      {
         report << CRLF << spaces(2) << p->first;
         found = true;
      }
   }

   if(!found) report << "none";
   report << CRLF << "Added testcases: ";
   found = false;

   for(auto c = currTests_.cbegin(); c != currTests_.cend(); ++c)
   {
      if(prevTests_.find(c->first) == prevTests_.cend())
      {
         report << CRLF << spaces(2) << c->first;
         found = true;
      }
   }

   if(!found) report << "none";
   report << CRLF << "Changed testcases: ";
   found = false;

   for(auto c = currTests_.cbegin(); c != currTests_.cend(); ++c)
   {
      auto p = prevTests_.find(c->first);

      if((p != prevTests_.cend()) && (c->second.hash != p->second.hash))
      {
         report << CRLF << spaces(2) << c->first;
         found = true;
      }
   }

   if(!found) report << "none";

   expl = report.str();
   return 0;
}

//------------------------------------------------------------------------------

fn_name CodeCoverage_Dump = "CodeCoverage.Dump";

word CodeCoverage::Dump(ostream& stream, string& expl)
{
   Debug::ft(CodeCoverage_Dump);

   if(currFuncs_.empty())
   {
      expl = "Database is empty.  Run >check first.";
      return -1;
   }

   std::set< string > inclTests;

   //  Update the current database with testcases that appear only in
   //  the previous database.
   //
   for(auto p = prevTests_.cbegin(); p != prevTests_.cend(); ++p)
   {
      if(currTests_.find(p->first) == currTests_.cend())
      {
         currTests_.insert(*p);
         inclTests.insert(p->first);
      }
   }

   //  If a function without a hash value appears only in the previous
   //  database, add it to the current database.  (A function *with* a
   //  hash value must have been deleted from the code base if it only
   //  appears in the previous database.)
   //
   for(auto p = prevFuncs_.cbegin(); p != prevFuncs_.cend(); ++p)
   {
      if((p->second.hash == UNHASHED) &&
         (currFuncs_.find(p->first) == currFuncs_.cend()))
      {
         currFuncs_.insert(*p);
      }
   }

   //  Look at functions that exist in both databases.  If a testcase invoked
   //  a function in the previous database, insert it as an invoker of that
   //  function in the current database *if the testcase was just added to the
   //  current database, above*.  (If the testcase wasn't added to the current
   //  database, it must have been re-executed, in which case it has already
   //  been inserted as an invoker of all its functions.)
   //
   for(auto pf = prevFuncs_.cbegin(); pf != prevFuncs_.cend(); ++pf)
   {
      auto cf = currFuncs_.find(pf->first);
      if(cf == currFuncs_.cend()) continue;

      auto& tests = pf->second.tests;
      for(auto pt = tests.cbegin(); pt != tests.cend(); ++pt)
      {
         if(inclTests.find(*pt) != inclTests.cend())
            cf->second.tests.insert(*pt);
      }
   }

   for(auto f = currFuncs_.cbegin(); f != currFuncs_.cend(); ++f)
   {
      stream << f->first << SPACE << std::hex << f->second.hash << std::dec;

      auto& tests = f->second.tests;

      if(!tests.empty())
      {
         for(auto t = tests.cbegin(); t != tests.cend(); ++t)
         {
            stream << SPACE << *t;
         }
      }

      stream << SPACE << DELIMITER << CRLF;
   }

   stream << DELIMITER << CRLF;

   for(auto t = currTests_.cbegin(); t != currTests_.cend(); ++t)
   {
      stream << t->first << SPACE << std::hex << t->second.hash;
      stream << std::dec << CRLF;
   }

   stream << DELIMITER << CRLF;
   expl = SuccessExpl;
   return 0;
}

//------------------------------------------------------------------------------

fn_name CodeCoverage_Erase = "CodeCoverage.Erase";

word CodeCoverage::Erase(string& item, bool prev, bool func, string& expl)
{
   Debug::ft(CodeCoverage_Erase);

   if(prevFuncs_.empty())
   {
      expl = "Database is empty.  Run >coverage load first.";
      return -1;
   }

   size_t found = 0;
   if(func) item = Mangle(item);

   if(prev)
   {
      if(func)
         found = prevFuncs_.erase(item);
      else
         found = prevTests_.erase(item);
   }
   else
   {
      if(func)
         found = currFuncs_.erase(item);
      else
         found = currTests_.erase(item);
   }

   if(found > 0)
   {
      expl = SuccessExpl;
      return 0;
   }

   expl = "No such entry: " + item;
   return -1;
}

//------------------------------------------------------------------------------

fn_name CodeCoverage_FindFiles = "CodeCoverage.FindFiles";

bool CodeCoverage::FindFiles
   (const char* dir, const char* ext, std::set< std::string >& names)
{
   Debug::ft(CodeCoverage_FindFiles);

   if(!SysFile::SetDir(dir)) return false;

   auto spec = "*" + string(ext);
   auto list = SysFile::GetFileList(nullptr, spec.c_str());

   if(list != nullptr)
   {
      string name;

      do
      {
         if(!list->IsSubdir())
         {
            list->GetName(name);
            auto pos = name.rfind(ext);
            name.erase(pos);
            names.insert(name);
         }
      }
      while(list->Advance());
   }

   return true;
}

//------------------------------------------------------------------------------

fn_name CodeCoverage_GetError = "CodeCoverage.GetError";

CodeCoverage::ImportState CodeCoverage::GetError
   (const string& reason, word& rc, string& expl)
{
   Debug::ft(CodeCoverage_GetError);

   expl = reason;
   rc = -1;
   return ImportError;
}

//------------------------------------------------------------------------------

fn_name CodeCoverage_GetFunc = "CodeCoverage.GetFunc";

CodeCoverage::ImportState CodeCoverage::GetFunc
   (string& input, word& rc, string& expl)
{
   Debug::ft(CodeCoverage_GetFunc);

   auto func = GetString(input);
   if(func.empty()) return GetFunction;
   if(func.front() == DELIMITER) return GetTestcase;
   auto hash = GetString(input);
   if(hash.empty() || !isxdigit(hash.front()))
      return GetError("Hash value for function missing", rc, expl);
   uint32_t n = std::stoul(hash, nullptr, 16);
   auto info = FuncInfo(n);
   auto result = prevFuncs_.insert(FuncData(func, info));
   if(!result.second)
      return GetError("Function name duplicated", rc, expl);
   currFunc_ = result.first;
   return GetTestcases;
}

//------------------------------------------------------------------------------

fn_name CodeCoverage_GetTest = "CodeCoverage.GetTest";

CodeCoverage::ImportState CodeCoverage::GetTest
   (string& input, word& rc, string& expl)
{
   Debug::ft(CodeCoverage_GetTest);

   auto test = GetString(input);
   if(test.empty()) return GetTestcase;
   if(test.front() == DELIMITER) return ImportDone;
   auto hash = GetString(input);
   if(hash.empty() || !isxdigit(hash.front()))
      return GetError("Hash value for testcase missing", rc, expl);
   uint32_t n = std::stoul(hash, nullptr, 16);
   auto info = TestInfo(n);
   auto result = prevTests_.insert(TestData(test, info));
   if(!result.second)
      return GetError("Testcase name duplicated", rc, expl);
   return GetTestcase;
}

//------------------------------------------------------------------------------

fn_name CodeCoverage_GetTests = "CodeCoverage.GetTests";

CodeCoverage::ImportState CodeCoverage::GetTests(string& input) const
{
   Debug::ft(CodeCoverage_GetTests);

   auto test = GetString(input);
   if(test.empty()) return GetTestcases;
   if(test.front() == DELIMITER) return GetFunction;
   currFunc_->second.tests.insert(test);
   return GetTestcases;
}

//------------------------------------------------------------------------------

fn_name CodeCoverage_Load = "CodeCoverage.Load";

word CodeCoverage::Load(std::istream& stream, string& expl)
{
   Debug::ft(CodeCoverage_Load);

   expl = "Error: explanation not provided";
   word rc = -1;
   string input;
   auto state = GetFunction;
   prevFuncs_.clear();
   prevTests_.clear();

   while(stream.peek() != EOF)
   {
      std::getline(stream, input);

      while(!input.empty())
      {
         switch(state)
         {
         case GetFunction:
            state = GetFunc(input, rc, expl);
            break;
         case GetTestcases:
            state = GetTests(input);
            break;
         case GetTestcase:
            state = GetTest(input, rc, expl);
            break;
         case ImportDone:
            expl = "Extra text in database: " + input;
            return -1;
         case ImportError:
         default:
            return rc;
         }
      }
   }

   if(state != ImportDone)
   {
      expl = "Parsing error: reached end of file unexpectedly";
      return -1;
   }

   expl = SuccessExpl;
   return 0;
}

//------------------------------------------------------------------------------

string CodeCoverage::Mangle(const string& s)
{
   string result(s);

   for(auto i = 0; i < result.size(); ++i)
   {
      if(result[i] == SPACE) result[i] = BLANK;
   }

   return result;
}

//------------------------------------------------------------------------------

fn_name CodeCoverage_Retest = "CodeCoverage.Retest";

word CodeCoverage::Retest(string& expl) const
{
   Debug::ft(CodeCoverage_Retest);

   if(prevFuncs_.empty())
   {
      expl = "Database is empty.  Run >coverage load first.";
      return -1;
   }

   if(currTests_.empty())
   {
      expl = "No testcases in database.  Run >coverage build first.";
      return -1;
   }

   std::vector< ConstFuncIter > modified;

   for(auto c = currFuncs_.cbegin(); c != currFuncs_.cend(); ++c)
   {
      if(prevFuncs_.find(c->first) == prevFuncs_.cend())
      {
         modified.push_back(c);
      }
   }

   for(auto c = currFuncs_.cbegin(); c != currFuncs_.cend(); ++c)
   {
      auto p = prevFuncs_.find(c->first);

      if((p != prevFuncs_.cend()) && (c->second.hash != p->second.hash))
      {
         modified.push_back(c);
      }
   }

   for(auto p = prevFuncs_.cbegin(); p!= prevFuncs_.cend(); ++p)
   {
      if((p->second.hash != UNHASHED) &&
         (currFuncs_.find(p->first) == currFuncs_.cend()))
      {
         modified.push_back(p);
      }
   }

   if(modified.empty())
   {
      expl = "No functions require retesting";
      return 0;
   }

   std::set< string > testless;
   std::set< string > tests;

   for(auto f = modified.cbegin(); f != modified.cend(); ++f)
   {
      auto& ft = (*f)->second.tests;

      if(ft.empty())
         testless.insert((*f)->first);
      else
         for(auto t = ft.cbegin(); t != ft.cend(); ++t) tests.insert(*t);
   }

   std::ostringstream report;

   if(!tests.empty())
   {
      report << "Testcases for modified functions:";

      for(auto t = tests.cbegin(); t != tests.cend(); ++t)
      {
         report << CRLF << spaces(2) << *t;
      }
   }

   if(!testless.empty())
   {
      if(!tests.empty()) report << CRLF;
      report << "No testcases exist for these modified functions:";

      for(auto f = testless.cbegin(); f != testless.cend(); ++f)
      {
         report << CRLF << spaces(2) << *f;
      }
   }

   expl = report.str();
   return 0;
}

//------------------------------------------------------------------------------

fn_name CodeCoverage_Stats = "CodeCoverage.Stats";

word CodeCoverage::Stats(string& expl) const
{
   Debug::ft(CodeCoverage_Stats);

   if(prevFuncs_.empty())
   {
      expl = "Database is empty.  Run >coverage load first.";
      return -1;
   }

   std::ostringstream stats;

   stats << "Functions: " << prevFuncs_.size() << CRLF;
   stats << "Testcases: " << prevTests_.size() << CRLF;
   stats << "Testcases per function:" << CRLF;

   const size_t MAX_TESTS = 10;
   size_t histogram[MAX_TESTS + 1] = { 0 };

   for(auto f = prevFuncs_.cbegin(); f != prevFuncs_.cend(); ++f)
   {
      if(f->second.tests.size() > MAX_TESTS)
         histogram[MAX_TESTS]++;
      else
         histogram[f->second.tests.size()]++;
   }

   for(auto i = 0; i < MAX_TESTS; ++i)
   {
      stats << std::setw(6) << i;
   }

   stats << std::setw(5) << MAX_TESTS << '+' << CRLF;

   for(auto i = 0; i <= MAX_TESTS; ++i)
   {
      stats << std::setw(6) << histogram[i];
   }

   expl = stats.str();
   return 0;
}

//------------------------------------------------------------------------------

fn_name CodeCoverage_Under = "CodeCoverage.Under";

word CodeCoverage::Under(size_t min, string& expl) const
{
   Debug::ft(CodeCoverage_Under);

   expl.clear();

   if(prevFuncs_.empty())
   {
      expl = "Database is empty.  Run >coverage load first.";
      return -1;
   }

   for(auto f = prevFuncs_.cbegin(); f != prevFuncs_.cend(); ++f)
   {
      if(f->second.tests.size() < min)
      {
         expl.append(f->first + CRLF);
      }
   }

   if(expl.empty())
      expl = "No such functions found.";
   else
      expl.pop_back();

   return 0;
}
}