/*
 * Copyright 2016 WebAssembly Community Group participants
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <cassert>
#include <cstdarg>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <string>

#include "config.h"

#include "src/binary-writer.h"
#include "src/binary-writer-spec.h"
#include "src/common.h"
#include "src/error-formatter.h"
#include "src/feature.h"
#include "src/filenames.h"
#include "src/ir.h"
#include "src/option-parser.h"
#include "src/resolve-names.h"
#include "src/stream.h"
#include "src/validator.h"
#include "src/wast-parser.h"

using namespace wabt;

static const char* s_infile;
static const char* s_outfile;
static int s_verbose;
static WriteBinaryOptions s_write_binary_options;
static bool s_validate = true;
static bool s_debug_parsing;
static Features s_features;

static std::unique_ptr<FileStream> s_log_stream;

static const char s_description[] =
R"(  read a file in the wasm spec test format, check it for errors, and
  convert it to a JSON file and associated wasm binary files.

examples:
  # parse spec-test.wast, and write files to spec-test.json. Modules are
  # written to spec-test.0.wasm, spec-test.1.wasm, etc.
  $ wast2json spec-test.wast -o spec-test.json
)";

static void ParseOptions(int argc, char* argv[]) {
  OptionParser parser("wast2json", s_description);

  parser.AddOption('v', "verbose", "Use multiple times for more info", []() {
    s_verbose++;
    s_log_stream = FileStream::CreateStdout();
  });
  parser.AddHelpOption();
  parser.AddOption("debug-parser", "Turn on debugging the parser of wast files",
                   []() { s_debug_parsing = true; });
  s_features.AddOptions(&parser);
  parser.AddOption('o', "output", "FILE", "output wasm binary file",
                   [](const char* argument) { s_outfile = argument; });
  parser.AddOption(
      'r', "relocatable",
      "Create a relocatable wasm binary (suitable for linking with e.g. lld)",
      []() { s_write_binary_options.relocatable = true; });
  parser.AddOption(
      "no-canonicalize-leb128s",
      "Write all LEB128 sizes as 5-bytes instead of their minimal size",
      []() { s_write_binary_options.canonicalize_lebs = false; });
  parser.AddOption("debug-names",
                   "Write debug names to the generated binary file",
                   []() { s_write_binary_options.write_debug_names = true; });
  parser.AddOption("no-check", "Don't check for invalid modules",
                   []() { s_validate = false; });
  parser.AddArgument("filename", OptionParser::ArgumentCount::One,
                     [](const char* argument) { s_infile = argument; });

  parser.Parse(argc, argv);
}

int ProgramMain(int argc, char** argv) {
  InitStdio();

  ParseOptions(argc, argv);

  std::unique_ptr<WastLexer> lexer = WastLexer::CreateFileLexer(s_infile);
  if (!lexer) {
    WABT_FATAL("unable to read file: %s\n", s_infile);
  }

  Errors errors;
  std::unique_ptr<Script> script;
  WastParseOptions parse_wast_options(s_features);
  Result result =
      ParseWastScript(lexer.get(), &script, &errors, &parse_wast_options);

  if (Succeeded(result)) {
    result = ResolveNamesScript(script.get(), &errors);

    if (Succeeded(result) && s_validate) {
      ValidateOptions options(s_features);
      result = ValidateScript(script.get(), &errors, options);
    }

    if (Succeeded(result)) {
      std::vector<FilenameMemoryStreamPair> module_streams;
      MemoryStream json_stream;

      std::string module_filename_noext(
          StripExtension(s_outfile ? s_outfile : s_infile));
      result = WriteBinarySpecScript(
          &json_stream, script.get(), s_infile, module_filename_noext,
          s_write_binary_options, &module_streams, s_log_stream.get());

      if (s_outfile) {
        json_stream.WriteToFile(s_outfile);
      }

      for (auto iter = module_streams.begin(); iter != module_streams.end();
           ++iter) {
        iter->stream->WriteToFile(iter->filename);
      }
    }
  }

  auto line_finder = lexer->MakeLineFinder();
  FormatErrorsToFile(errors, Location::Type::Text, line_finder.get());

  return result != Result::Ok;
}

int main(int argc, char** argv) {
  WABT_TRY
  return ProgramMain(argc, argv);
  WABT_CATCH_BAD_ALLOC_AND_EXIT
}
