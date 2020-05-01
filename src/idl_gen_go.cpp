/*
 * Copyright 2014 Google Inc. All rights reserved.
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

// independent from idl_parser, since this code is not needed for most clients

#include <sstream>
#include <string>

#include "flatbuffers/code_generators.h"
#include "flatbuffers/flatbuffers.h"
#include "flatbuffers/flatc.h"
#include "flatbuffers/idl.h"
#include "flatbuffers/util.h"

#ifdef _WIN32
#  include <direct.h>
#  define PATH_SEPARATOR "\\"
#  define mkdir(n, m) _mkdir(n)
#else
#  include <sys/stat.h>
#  define PATH_SEPARATOR "/"
#endif

namespace flatbuffers {

namespace go {

// see https://golang.org/ref/spec#Keywords
// some keyword that used in github.com/google/flatbuffers/go builder:
// Table object, Struct object , Union object,  String() func
static const char *const g_golang_keywords[] = {
  "break",       "default", "func",     "interface", "select",      "case",
  "defer",       "go",      "map",      "struct",    "chan",        "else",
  "goto",        "package", "switch",   "const",     "fallthrough", "if",
  "range",       "type",    "continue", "for",       "import",      "return",
  "var",         "table",   "struct",   "union",     "string",      "builder",
  "flatbuffers", "init"
};

static std::string GoIdentity(const std::string &name, bool first = true) {
  for (size_t i = 0;
       i < sizeof(g_golang_keywords) / sizeof(g_golang_keywords[0]); i++) {
    if (name == g_golang_keywords[i]) { return MakeCamel(name + "_", first); }
  }

  return MakeCamel(name, first);
}

class GoGenerator : public BaseGenerator {
 public:
  GoGenerator(const Parser &parser, const std::string &path,
              const std::string &file_name, const std::string &go_namespace)
      : BaseGenerator(parser, path, file_name, "" /* not used*/,
                      "" /* not used */, "go"),
        cur_name_space_(nullptr) {
    std::istringstream iss(go_namespace);
    std::string component;
    while (std::getline(iss, component, '.')) {
      go_namespace_.components.push_back(component);
    }
  }

  bool generate() {
    std::string one_file_code;
    bool needs_imports;
    // handle generate_all options
    if (parser_.opts.generate_object_based_api) {
      generate_object_based_api = true;
    }
    if (parser_.opts.mutable_buffer) { mutable_buffer = true; }
    if (parser_.opts.generate_all) {
      generate_object_based_api = true;
      mutable_buffer = true;
    }
    // generate enums and union
    for (auto it = parser_.enums_.vec.begin(); it != parser_.enums_.vec.end();
         ++it) {
      tracked_imported_namespaces_.clear();
      needs_imports = false;
      std::string enumcode;

      GenEnum(**it, &enumcode, &needs_imports);

      if (parser_.opts.one_file) {
        one_file_code += enumcode;
      } else {
        if (!SaveType(**it, &enumcode, needs_imports, true)) return false;
      }
    }

    // generate  table / struct
    for (auto it = parser_.structs_.vec.begin();
         it != parser_.structs_.vec.end(); ++it) {
      tracked_imported_namespaces_.clear();
      std::string declcode;

      GenStruct(**it, &declcode);

      if (parser_.opts.one_file) {
        one_file_code += declcode;
      } else {
        if (!SaveType(**it, &declcode, true, false)) return false;
      }
    }

    if (parser_.opts.one_file) {
      std::string code = "";
      const bool is_enum = !parser_.enums_.vec.empty();

      // check multiple namespace
      if (parser_.namespaces_.size() > 1) {
        LogCompilerError(
            "can't use --gen-onefile parameter and --go when more the one "
            "namespace defined.");
      }

      // namespace is missing
      if (go_namespace_.components.empty()) {
        if (parser_.root_struct_def_) {
          go_namespace_.components.push_back(parser_.root_struct_def_->name);
        } else {
          LogCompilerError(
              "missing golang namespace, please use --go-namespace parameter "
              "or set namespace in IDL file");
        }
      }

      BeginFile(LastNamespacePart(go_namespace_), true, is_enum, &code);
      code += one_file_code;
      const std::string filename =
          GeneratedFileName(path_, file_name_, parser_.opts);
      return SaveFile(filename.c_str(), code, false);
    }

    return true;
  }

 private:
  Namespace go_namespace_;
  Namespace *cur_name_space_;
  bool generate_object_based_api = false;
  bool mutable_buffer = false;

  struct NamespacePtrLess {
    bool operator()(const Namespace *a, const Namespace *b) const {
      return *a < *b;
    }
  };
  std::set<const Namespace *, NamespacePtrLess> tracked_imported_namespaces_;

  // Most field accessors need to retrieve and test the field offset first,
  // this is the prefix code for that.
  std::string OffsetPrefix(const FieldDef &field) {
    return "{\n\to := flatbuffers.UOffsetT(rcv._tab.Offset(" +
           NumToString(field.value.offset) + "))\n\tif o != 0 {\n";
  }

  // Begin a class declaration.
  void BeginClass(const StructDef &struct_def, std::string *code_ptr) {
    std::string &code = *code_ptr;

    code += "type " + struct_def.name + " struct {\n\t";

    // _ is reserved in flatbuffers field names, so no chance of name conflict:
    code += "_tab ";
    code += struct_def.fixed ? "flatbuffers.Struct" : "flatbuffers.Table";
    code += "\n}\n\n";
  }

  // Construct the name of the type for this enum.
  std::string GetEnumTypeName(const EnumDef &enum_def) {
    return WrapInNameSpaceAndTrack(enum_def.defined_namespace,
                                   GoIdentity(enum_def.name));
  }

  // Create a type for the enum values.
  void GenEnumType(const EnumDef &enum_def, std::string *code_ptr) {
    std::string &code = *code_ptr;
    code += "type " + GetEnumTypeName(enum_def) + " ";
    code += GenTypeBasic(enum_def.underlying_type) + "\n\n";
  }

  // Begin enum code with a class declaration.
  void BeginEnum(std::string *code_ptr) {
    std::string &code = *code_ptr;
    code += "const (\n";
  }

  // A single enum member.
  void EnumMember(const EnumDef &enum_def, const EnumVal &ev,
                  size_t max_name_length, std::string *code_ptr) {
    std::string &code = *code_ptr;
    code += "\t";
    code += enum_def.name;
    code += ev.name;
    code += " ";
    code += std::string(max_name_length - ev.name.length(), ' ');
    code += GetEnumTypeName(enum_def);
    code += " = ";
    code += enum_def.ToString(ev) + "\n";
  }

  // End enum code.
  void EndEnum(const EnumDef &enum_def, std::string *code_ptr) {
    std::string &code = *code_ptr;

    if (!(enum_def.attributes.Lookup("bit_flags"))) {
      const EnumVal *minv = enum_def.MinValue();
      const EnumVal *maxv = enum_def.MaxValue();
      code += "\n\t";
      code += enum_def.name;
      code += "VerifyValueMin ";
      code += GetEnumTypeName(enum_def);
      code += " = ";
      code += enum_def.ToString(*minv) + "\n";

      code += "\t";
      code += enum_def.name;
      code += "VerifyValueMax ";
      code += GetEnumTypeName(enum_def);
      code += " = ";
      code += enum_def.ToString(*maxv) + "\n";
    }
    code += ")\n\n";
  }

  // Begin enum name map.
  void BeginEnumNames(const EnumDef &enum_def, std::string *code_ptr) {
    std::string &code = *code_ptr;
    code += "var EnumNames";
    code += enum_def.name;
    code += " = map[" + GetEnumTypeName(enum_def) + "]string{\n";
  }

  // A single enum name member.
  void EnumNameMember(const EnumDef &enum_def, const EnumVal &ev,
                      size_t max_name_length, std::string *code_ptr) {
    std::string &code = *code_ptr;
    code += "\t";
    code += enum_def.name;
    code += ev.name;
    code += ": ";
    code += std::string(max_name_length - ev.name.length(), ' ');
    code += "\"";
    code += ev.name;
    code += "\",\n";
  }

  // End enum name map.
  void EndEnumNames(std::string *code_ptr) {
    std::string &code = *code_ptr;
    code += "}\n\n";
  }

  // Generate String() method on enum type.
  void EnumStringer(const EnumDef &enum_def, std::string *code_ptr) {
    std::string &code = *code_ptr;
    code += "func (v " + enum_def.name + ") String() string {\n";
    code += "\tif s, ok := EnumNames" + enum_def.name + "[v]; ok {\n";
    code += "\t\treturn s\n";
    code += "\t}\n";
    code += "\treturn \"" + enum_def.name;
    code += "(\" + strconv.FormatInt(int64(v), 10) + \")\"\n";
    code += "}\n\n";
  }

  // Begin enum value map.
  void BeginEnumValues(const EnumDef &enum_def, std::string *code_ptr) {
    std::string &code = *code_ptr;
    code += "var EnumValues";
    code += enum_def.name;
    code += " = map[string]" + GetEnumTypeName(enum_def) + "{\n";
  }

  // A single enum value member.
  void EnumValueMember(const EnumDef &enum_def, const EnumVal &ev,
                       size_t max_name_length, std::string *code_ptr) {
    std::string &code = *code_ptr;
    code += "\t\"";
    code += ev.name;
    code += "\": ";
    code += std::string(max_name_length - ev.name.length(), ' ');
    code += enum_def.name;
    code += ev.name;
    code += ",\n";
  }

  // End enum value map.
  void EndEnumValues(std::string *code_ptr) {
    std::string &code = *code_ptr;
    code += "}\n\n";
  }

  // root table accessor / embed table accessor
  void NewRootTypeFromBuffer(const StructDef &struct_def,
                             std::string *code_ptr) {
    std::string &code = *code_ptr;

    // root table accessor
    code += "// GetRootAs";
    code += struct_def.name;
    code += " shortcut to access root table\n";
    code += "func GetRootAs";
    code += struct_def.name;
    code += "(buf []byte, offset flatbuffers.UOffsetT) ";
    code += "*" + struct_def.name + "";
    code += " {\n";
    code += "\tn := flatbuffers.GetUOffsetT(buf[offset:])\n";
    code += "\tx := &" + struct_def.name + "{}\n";
    code += "\tx.Init(buf, n+offset)\n";
    code += "\treturn x\n";
    code += "}\n\n";

    // access table inside unions vector
    code += "// GetTableVectorAs";
    code += struct_def.name;
    code += " shortcut to access table in vector of  unions\n";
    code += "func GetTableVectorAs";
    code += struct_def.name;
    code += "(table *flatbuffers.Table) ";
    code += "*" + struct_def.name + "";
    code += " {\n";
    code += "\tn := flatbuffers.GetUOffsetT(table.Bytes[table.Pos:])\n";
    code += "\tx := &" + struct_def.name + "{}\n";
    code += "\tx.Init(table.Bytes, n+table.Pos)\n";
    code += "\treturn x\n";
    code += "}\n\n";

    // access table in single union field
    code += "// GetTableAs";
    code += struct_def.name;
    code += " shortcut to access table in single union field\n";
    code += "func GetTableAs";
    code += struct_def.name;
    code += "(table *flatbuffers.Table) ";
    code += "*" + struct_def.name + "";
    code += " {\n";
    code += "\tx := &" + struct_def.name + "{}\n";
    code += "\tx.Init(table.Bytes, table.Pos)\n";
    code += "\treturn x\n";
    code += "}\n\n";
  }
  // struct accessor
  void NewStructTypeFromBuffer(const StructDef &struct_def,
                               std::string *code_ptr) {
    std::string &code = *code_ptr;

    // struct accessor inside  unions vector
    code += "// GetStructVectorAs";
    code += struct_def.name;
    code += " shortcut to access struct in vector of unions\n";
    code += "func GetStructVectorAs";
    code += struct_def.name;
    code += "(table *flatbuffers.Table) ";
    code += "*" + struct_def.name + "";
    code += " {\n";
    code += "\tn := flatbuffers.GetUOffsetT(table.Bytes[table.Pos:])\n";
    code += "\tx := &" + struct_def.name + "{}\n";
    code += "\tx.Init(table.Bytes, n+table.Pos)\n";
    code += "\treturn x\n";
    code += "}\n\n";

    // struct accessor in union alone
    code += "// GetStructAs";
    code += struct_def.name;
    code += " shortcut to access struct in single union field\n";
    code += "func GetStructAs";
    code += struct_def.name;
    code += "(table *flatbuffers.Table) ";
    code += "*" + struct_def.name + "";
    code += " {\n";
    code += "\tx := &" + struct_def.name + "{}\n";
    code += "\tx.Init(table.Bytes, table.Pos)\n";
    code += "\treturn x\n";
    code += "}\n\n";
  }
  // Initialize an existing object with other data, to avoid an allocation.
  void InitializeExisting(const StructDef &struct_def, std::string *code_ptr) {
    std::string &code = *code_ptr;

    GenReceiver(struct_def, code_ptr);
    code += " Init(buf []byte, i flatbuffers.UOffsetT) ";
    code += "{\n";
    code += "\trcv._tab.Bytes = buf\n";
    code += "\trcv._tab.Pos = i\n";
    code += "}\n\n";
  }

  // Implement the table accessor
  void GenTableAccessor(const StructDef &struct_def, std::string *code_ptr) {
    std::string &code = *code_ptr;

    GenReceiver(struct_def, code_ptr);
    code += " Table() flatbuffers.Table ";
    code += "{\n";

    if (struct_def.fixed) {
      code += "\treturn rcv._tab.Table\n";
    } else {
      code += "\treturn rcv._tab\n";
    }
    code += "}\n\n";
  }

  // Get the length of a vector.
  void GetVectorLen(const StructDef &struct_def, const FieldDef &field,
                    std::string *code_ptr) {
    std::string &code = *code_ptr;

    GenReceiver(struct_def, code_ptr);
    code += " " + GoIdentity(field.name) + "Length(";
    code += ") int " + OffsetPrefix(field);
    code += "\t\treturn rcv._tab.VectorLen(o)\n\t}\n";
    code += "\treturn 0\n}\n\n";
  }

  // Get a [ubyte] vector as a byte slice.
  void GetUByteSlice(const StructDef &struct_def, const FieldDef &field,
                     std::string *code_ptr) {
    std::string &code = *code_ptr;

    GenReceiver(struct_def, code_ptr);
    code += " " + GoIdentity(field.name) + "Bytes(";
    code += ") []byte " + OffsetPrefix(field);
    code += "\t\treturn rcv._tab.ByteVector(o + rcv._tab.Pos)\n\t}\n";
    code += "\treturn nil\n}\n\n";
  }

  // Get the value of a struct's scalar.
  void GetScalarFieldOfStruct(const StructDef &struct_def,
                              const FieldDef &field, std::string *code_ptr) {
    std::string &code = *code_ptr;
    std::string getter = GenGetter(field.value.type);
    GenReceiver(struct_def, code_ptr);
    code += " " + GoIdentity(field.name);
    code += "() " + TypeName(field) + " {\n";
    code += "\treturn " +
            CastToEnum(field.value.type,
                       getter + "(rcv._tab.Pos + flatbuffers.UOffsetT(" +
                           NumToString(field.value.offset) + "))");
    code += "\n}\n\n";
  }

  // Get the value of a table's scalar.
  void GetScalarFieldOfTable(const StructDef &struct_def, const FieldDef &field,
                             std::string *code_ptr) {
    std::string &code = *code_ptr;
    std::string getter = GenGetter(field.value.type);
    GenReceiver(struct_def, code_ptr);
    code += " " + GoIdentity(field.name);
    code += "() " + TypeName(field) + " ";
    code += OffsetPrefix(field) + "\t\treturn ";
    code += CastToEnum(field.value.type, getter + "(o + rcv._tab.Pos)");
    code += "\n\t}\n";
    code += "\treturn " + GenConstant(field) + "\n";
    code += "}\n\n";
  }

  // Get a struct by initializing an existing struct.
  // Specific to Struct.
  void GetStructFieldOfStruct(const StructDef &struct_def,
                              const FieldDef &field, std::string *code_ptr) {
    std::string &code = *code_ptr;
    GenReceiver(struct_def, code_ptr);
    code += " " + GoIdentity(field.name);
    code += "(obj *" + TypeName(field);
    code += ") *" + TypeName(field);
    code += " {\n";
    code += "\tif obj == nil {\n";
    code += "\t\tobj = new(" + TypeName(field) + ")\n";
    code += "\t}\n";
    code += "\tobj.Init(rcv._tab.Bytes, rcv._tab.Pos+";
    code += NumToString(field.value.offset) + ")";
    code += "\n\treturn obj\n";
    code += "}\n\n";
  }

  // Get a struct by initializing an existing struct.
  // Specific to Table.
  void GetStructFieldOfTable(const StructDef &struct_def, const FieldDef &field,
                             std::string *code_ptr) {
    std::string &code = *code_ptr;
    GenReceiver(struct_def, code_ptr);
    code += " " + GoIdentity(field.name);
    code += "(obj *";
    code += TypeName(field);
    code += ") *" + TypeName(field) + " " + OffsetPrefix(field);
    if (field.value.type.struct_def->fixed) {
      code += "\t\tx := o + rcv._tab.Pos\n";
    } else {
      code += "\t\tx := rcv._tab.Indirect(o + rcv._tab.Pos)\n";
    }
    code += "\t\tif obj == nil {\n";
    code += "\t\t\tobj = new(" + TypeName(field) + ")\n";
    code += "\t\t}\n";
    code += "\t\tobj.Init(rcv._tab.Bytes, x)\n";
    code += "\t\treturn obj\n\t}\n\treturn nil\n";
    code += "}\n\n";
  }

  // Get the value of a string.
  void GetStringField(const StructDef &struct_def, const FieldDef &field,
                      std::string *code_ptr) {
    std::string &code = *code_ptr;
    GenReceiver(struct_def, code_ptr);
    code += " " + GoIdentity(field.name);
    code += "() " + TypeName(field) + " ";
    code += OffsetPrefix(field) + "\t\treturn " + GenGetter(field.value.type);
    code += "(o + rcv._tab.Pos)\n\t}\n\treturn nil\n";
    code += "}\n\n";
  }

  // Get the value of a union from an object.
  void GetUnionField(const StructDef &struct_def, const FieldDef &field,
                     std::string *code_ptr) {
    std::string &code = *code_ptr;
    GenReceiver(struct_def, code_ptr);
    code += " " + GoIdentity(field.name) + "(";
    code += "obj " + GenTypePointer(field.value.type) + ") bool ";
    code += OffsetPrefix(field);
    code += "\t\t" + GenGetter(field.value.type);
    code += "(obj, o)\n\t\treturn true\n\t}\n";
    code += "\treturn false\n";
    code += "}\n\n";
  }

  // Get the value of a vector's struct member.
  void GetMemberOfVectorOfStruct(const StructDef &struct_def,
                                 const FieldDef &field, std::string *code_ptr) {
    std::string &code = *code_ptr;
    auto vectortype = field.value.type.VectorType();

    GenReceiver(struct_def, code_ptr);
    code += " " + GoIdentity(field.name);
    code += "(obj *" + TypeName(field);
    code += ", j int) bool " + OffsetPrefix(field);
    code += "\t\tx := rcv._tab.Vector(o)\n";
    code += "\t\tx += flatbuffers.UOffsetT(j) * ";
    code += NumToString(InlineSize(vectortype)) + "\n";
    if (!(vectortype.struct_def->fixed)) {
      code += "\t\tx = rcv._tab.Indirect(x)\n";
    }
    code += "\t\tobj.Init(rcv._tab.Bytes, x)\n";
    code += "\t\treturn true\n\t}\n";
    code += "\treturn false\n";
    code += "}\n\n";
  }
  // Get the value of a vector's non-struct member.
  void GetMemberOfVectorOfUnions(const StructDef &struct_def,
                                 const FieldDef &field, std::string *code_ptr) {
    std::string &code = *code_ptr;

    GenReceiver(struct_def, code_ptr);
    code += " " + GoIdentity(field.name);
    code += "(j int, obj *flatbuffers.Table) bool ";
    code += OffsetPrefix(field);
    code += "\t\ta := rcv._tab.Vector(o)\n";
    code += "\t\tobj.Pos = a + flatbuffers.UOffsetT(j*4)\n";
    code += "\t\tobj.Bytes = rcv._tab.Bytes\n";
    code += "\t\treturn true\n";
    code += "\t}\n";
    code += "\treturn false\n";
    code += "}\n\n";
  }

  // Get the value of a vector's non-struct member.
  void GetMemberOfVectorOfNonStruct(const StructDef &struct_def,
                                    const FieldDef &field,
                                    std::string *code_ptr) {
    std::string &code = *code_ptr;
    auto vectortype = field.value.type.VectorType();

    GenReceiver(struct_def, code_ptr);
    code += " " + GoIdentity(field.name);
    code += "(j int) " + TypeName(field) + " ";
    code += OffsetPrefix(field);
    code += "\t\ta := rcv._tab.Vector(o)\n";
    code += "\t\treturn " +
            CastToEnum(field.value.type,
                       GenGetter(field.value.type) +
                           "(a + flatbuffers.UOffsetT(j*" +
                           NumToString(InlineSize(vectortype)) + "))");
    code += "\n\t}\n";
    if (vectortype.base_type == BASE_TYPE_STRING) {
      code += "\treturn nil\n";
    } else if (vectortype.base_type == BASE_TYPE_BOOL) {
      code += "\treturn false\n";
    } else {
      code += "\treturn 0\n";
    }
    code += "}\n\n";
  }

  // Begin the creator function signature.
  void BeginBuilderArgs(const StructDef &struct_def, std::string *code_ptr) {
    std::string &code = *code_ptr;

    if (code.substr(code.length() - 2) != "\n\n") {
      // a previous mutate has not put an extra new line
      code += "\n";
    }
    code += "func Create" + GoIdentity(struct_def.name);
    code += "(builder *flatbuffers.Builder";
  }

  // Recursively generate arguments for a constructor, to deal with nested
  // structs.
  void StructBuilderArgs(const StructDef &struct_def, const char *nameprefix,
                         std::string *code_ptr) {
    std::string nl = ", ";
    if (struct_def.fields.vec.size() > 3) { nl = ", \n\t"; }

    for (auto it = struct_def.fields.vec.begin();
         it != struct_def.fields.vec.end(); ++it) {
      auto &field = **it;

      // struct field
      if ((IsStruct(field.value.type)) && (!IsArray(field.value.type))) {
        // Generate arguments for a struct inside a struct. To ensure names
        // don't clash, and to make it obvious these arguments are constructing
        // a nested struct, prefix the name with the field name.
        StructBuilderArgs(*field.value.type.struct_def,
                          (nameprefix + (field.name + "__")).c_str(), code_ptr);
      }  // array field
      else if (IsArray(field.value.type)) {
        // fixed length struct parameter
        if (IsStruct(field.value.type.VectorType())) {
          auto end = field.value.type.fixed_length;
          auto start = 0;
          for (int i = start; i < end; i++) {
            StructBuilderArgs(
                *field.value.type.struct_def,
                (nameprefix + (field.name + NumToString(i) + "_")).c_str(),
                code_ptr);
          }
        }
        // struct
        else {
          // fixed length array scalar parameter
          std::string &code = *code_ptr;
          code += nl + nameprefix;
          code += GoIdentity(field.name, false);
          code += " [" + NumToString(field.value.type.fixed_length) + "]";
          code += NativeType(field.value.type.VectorType());

        }  //
        // end
        //
      }  // other field
      else {
        std::string &code = *code_ptr;
        code += nl + nameprefix;
        code += GoIdentity(field.name, false);
        code += " " + TypeName(field);
      }
    }
  }

  // End the creator function signature.
  void EndBuilderArgs(std::string *code_ptr) {
    std::string &code = *code_ptr;
    code += ") flatbuffers.UOffsetT {\n";
  }

  // Recursively generate struct construction statements and insert manual
  // padding.
  void StructBuilderBody(const StructDef &struct_def, const char *nameprefix,
                         std::string *code_ptr) {
    std::string &code = *code_ptr;
    code += "\tbuilder.Prep(" + NumToString(struct_def.minalign) + ", ";
    code += NumToString(struct_def.bytesize) + ")\n";
    // each field
    for (auto it = struct_def.fields.vec.rbegin();
         it != struct_def.fields.vec.rend(); ++it) {
      auto &field = **it;

      if (field.padding)
        code += "\tbuilder.Pad(" + NumToString(field.padding) + ")\n";

      if (IsStruct(field.value.type)) {
        StructBuilderBody(*field.value.type.struct_def,
                          (nameprefix + (field.name + "_")).c_str(), code_ptr);
      } else {
        code += "\tbuilder.Prepend" + GenMethod(field) + "(";
        code += CastToBaseType(field.value.type,
                               nameprefix + GoIdentity(field.name, false)) +
                ")\n";
      }
    }  //
  }

  // Recursively generate struct construction statements and insert manual
  // padding.
  // support fixed-length array
  void StructFixedBuilderBody(const StructDef &struct_def,
                              const char *nameprefix, std::string *code_ptr,
                              bool prep = false) {
    std::string &code = *code_ptr;
    // bypass builder.Prep when build struct inside a struct
    if (!prep) {
      code += "\tbuilder.Prep(" + NumToString(struct_def.minalign) + ", ";
      code += NumToString(struct_def.bytesize) + ")\n";
    }
    // each field
    for (auto it = struct_def.fields.vec.rbegin();
         it != struct_def.fields.vec.rend(); ++it) {
      const auto &field = **it;
      const auto &field_type = field.value.type;
      //
      auto is_fixed_length_array = false;
      auto fixed_length = 0;
      auto is_enum_field = false;
      auto is_enum_element = false;
      auto is_struct_field = false;
      auto is_struct_element = false;

      if (field_type.fixed_length > 0) {
        is_fixed_length_array = true;
        fixed_length = field_type.fixed_length;
      }

      if (field_type.enum_def != nullptr) { is_enum_field = true; }
      if (IsStruct(field_type)) { is_struct_field = true; }
      if (IsStruct(field_type.VectorType())) { is_struct_element = true; }
      if (field_type.VectorType().enum_def != nullptr) {
        is_enum_element = true;
      }

      // ------------------------------------------
      // padding
      if (field.padding > 0) {
        code += "\tbuilder.Pad(" + NumToString(field.padding) + ")\n";
      }
      // ------------------------------------------
      code += "\t// offset: " + NumToString(field.value.offset) + "\n";

      // scalar field
      if ((!is_fixed_length_array) && (!is_enum_field) && (!is_struct_field) &&
          (!is_struct_element)) {
        code += "\tbuilder.Prepend" + GenMethod(field) + "(";
        code += CastToBaseType(field.value.type,
                               nameprefix + GoIdentity(field.name, false)) +
                ")\n";
        // continue;
      } else
          // scalar fixed length array field
          if ((is_fixed_length_array) && !(is_enum_element) &&
              !(is_struct_element)) {
        code += "\t";
        code += nameprefix;
        code += GoIdentity(field.name, false);

        code += "_length := " + NumToString(fixed_length) + "\n";
        code += "\tfor _j := (";
        code += nameprefix;
        code += GoIdentity(field.name, false) + "_length -1); ";
        code += "_j >= 0; _j-- {\n";
        code += "\t\tbuilder.Prepend" +
                MakeCamel(GenTypeBasic(field.value.type.VectorType())) + "(";
        code += nameprefix;
        code += GoIdentity(field.name, false);
        code += "[_j])\n";
        code += "\t}\n";
        //        continue;
      } else
          // enum fixed length array field
          if ((is_fixed_length_array) && (is_enum_element)) {
        code += "\t";
        code += nameprefix;
        code += GoIdentity(field.name, false);
        code += "_length := " + NumToString(fixed_length) + "\n";
        code += "\tfor _j := (";
        code += nameprefix;
        code += GoIdentity(field.name, false) + "_length -1); ";
        code += "_j >= 0; _j-- {\n";
        code += "\t\tbuilder.Prepend" +
                MakeCamel(GenTypeBasic(field.value.type.VectorType())) + "(";
        code += GenTypeBasic(field.value.type.VectorType()) + "(";
        code += nameprefix;
        code += GoIdentity(field.name, false);
        code += "[_j]))\n";
        code += "\t}\n";
        //        continue;
      } else

          // single struct field  but not array
          if (is_struct_field) {
        code += "// build struct " + GoIdentity(field.name) + "\n";
        StructFixedBuilderBody(*field.value.type.struct_def,
                               (nameprefix + (field.name + "__")).c_str(),
                               code_ptr, true);
        //        continue;
      } else
          // struct fixed length array field
          if ((is_fixed_length_array) && (is_struct_element)) {
        auto start = 0;
        for (int i = start; i < fixed_length; i++) {
          code += "// build struct " + GoIdentity(field.name) + "\n";
          StructFixedBuilderBody(
              *field.value.type.struct_def,
              (nameprefix + (field.name + NumToString(i) + "_")).c_str(),
              code_ptr, true);
        }
        //        continue;
      } else
          // single enum field
          if (is_enum_field) {
        code += "\tbuilder.Prepend" +
                MakeCamel(GenTypeBasic(field.value.type)) + "(";
        code += GenTypeBasic(field.value.type) + "(";
        code += nameprefix;
        code += GoIdentity(field.name, false);
        code += "))\n";
      }
    }  // end loop
  }

  void EndBuilderBody(std::string *code_ptr) {
    std::string &code = *code_ptr;
    code += "\treturn builder.Offset()\n";
    code += "}\n\n";
  }

  // Get the value of a table's starting offset.
  void GetStartOfTable(const StructDef &struct_def, std::string *code_ptr) {
    std::string &code = *code_ptr;
    code += "func " + struct_def.name + "Start";
    code += "(builder *flatbuffers.Builder) {\n";
    code += "\tbuilder.StartObject(";
    code += NumToString(struct_def.fields.vec.size());
    code += ")\n}\n\n";
  }

  // Set the value of a table's field.
  void BuildFieldOfTable(const StructDef &struct_def, const FieldDef &field,
                         const size_t offset, std::string *code_ptr) {
    std::string &code = *code_ptr;
    // field builder
    code += "func " + struct_def.name + "Add" + GoIdentity(field.name);
    code += "(builder *flatbuffers.Builder, ";
    code += GoIdentity(field.name, false) + " ";

    if (!IsScalar(field.value.type.base_type) && (!struct_def.fixed)) {
      code += "flatbuffers.UOffsetT";
    } else {
      code += TypeName(field);
    }
    code += ") {\n";
    code += "\tbuilder.Prepend";
    code += GenMethod(field) + "Slot(";
    code += NumToString(offset) + ", ";
    if (!IsScalar(field.value.type.base_type) && (!struct_def.fixed)) {
      code += "flatbuffers.UOffsetT";
      code += "(";
      code += GoIdentity(field.name, false) + ")";
    } else {
      code += CastToBaseType(field.value.type, GoIdentity(field.name, false));
    }
    code += ", " + GenConstant(field);
    code += ")\n}\n\n";
  }

  // Set the value of one of the members of a table's vector.
  void BuildVectorOfTable(const StructDef &struct_def, const FieldDef &field,
                          std::string *code_ptr) {
    std::string &code = *code_ptr;
    // vector start
    code += "func " + struct_def.name + "Start";
    code += GoIdentity(field.name);
    code += "Vector(builder *flatbuffers.Builder, numElems int) {\n";
    code += "\tbuilder.StartVector(";
    auto vector_type = field.value.type.VectorType();
    auto alignment = InlineAlignment(vector_type);
    auto elem_size = InlineSize(vector_type);
    code += NumToString(elem_size);
    code += ", numElems, " + NumToString(alignment);
    code += ")\n}\n\n";
    // vector end
    code += "func " + struct_def.name + "End";
    code += GoIdentity(field.name);
    code += "Vector(builder *flatbuffers.Builder, numElems int) ";
    code += "flatbuffers.UOffsetT {\n";
    code += "\treturn builder.EndVector(numElems)\n}\n\n";
  }

  // Get the offset of the end of a table.
  void GetEndOffsetOnTable(const StructDef &struct_def, std::string *code_ptr) {
    std::string &code = *code_ptr;
    code += "func " + struct_def.name + "End";
    code += "(builder *flatbuffers.Builder) flatbuffers.UOffsetT ";
    code += "{\n\treturn builder.EndObject()\n}\n";
  }

  // Generate the receiver for function signatures.
  void GenReceiver(const StructDef &struct_def, std::string *code_ptr) {
    std::string &code = *code_ptr;
    code += "func (rcv *" + struct_def.name + ")";
  }

  // Generate a struct field getter, conditioned on its child type(s).
  void GenStructAccessor(const StructDef &struct_def, const FieldDef &field,
                         std::string *code_ptr) {
    auto &field_type = field.value.type;

    GenComment(field.doc_comment, code_ptr, nullptr, "");

    if (IsScalar(field_type.base_type)) {
      if (struct_def.fixed) {
        GetScalarFieldOfStruct(struct_def, field, code_ptr);
      } else {
        GetScalarFieldOfTable(struct_def, field, code_ptr);
      }
    } else {
      switch (field_type.base_type) {
          // struct
        case BASE_TYPE_STRUCT:
          if (struct_def.fixed) {
            GetStructFieldOfStruct(struct_def, field, code_ptr);
          } else {
            GetStructFieldOfTable(struct_def, field, code_ptr);
          }
          break;
        case BASE_TYPE_STRING:
          GetStringField(struct_def, field, code_ptr);
          break;
        case BASE_TYPE_VECTOR: {
          auto vectortype = field.value.type.VectorType();

          GetVectorLen(struct_def, field, code_ptr);

          if (field_type.element == BASE_TYPE_UCHAR) {
            GetUByteSlice(struct_def, field, code_ptr);
            break;
          }

          // vector of unions ( unions in array )
          if (vectortype.base_type == BASE_TYPE_UNION) {
            GetMemberOfVectorOfUnions(struct_def, field, code_ptr);
            break;
          }
          // struct
          if (vectortype.base_type == BASE_TYPE_STRUCT) {
            GetMemberOfVectorOfStruct(struct_def, field, code_ptr);
            break;
          }
          // others
          GetMemberOfVectorOfNonStruct(struct_def, field, code_ptr);
          break;
        }
        case BASE_TYPE_UNION: GetUnionField(struct_def, field, code_ptr); break;
        case BASE_TYPE_ARRAY: {
          GetFixedArray(struct_def, field, code_ptr);
          break;
        }
        default: FLATBUFFERS_ASSERT(0);
      }
    }
  }

  // Get the value of a union from an object.
  void GetFixedArray(const StructDef &struct_def, const FieldDef &field,
                     std::string *code_ptr) {
    //    std::string &code = *code_ptr;
    if (struct_def.fixed) {  //
      //  code += "// fixed struct array " + GoIdentity(field.name) + "\n";
      GetFixedScalarFieldOfStruct(struct_def, field, code_ptr);
    } else {
      //  code += "// fixed scalar array " + GoIdentity(field.name) + "\n";
      GetFixedScalarFieldOfTable(struct_def, field, code_ptr);
    }
  }

  // Get the value of a struct's scalar.
  void GetFixedScalarFieldOfStruct(const StructDef &struct_def,
                                   const FieldDef &field,
                                   std::string *code_ptr) {
    std::string &code = *code_ptr;
    auto vectortype = field.value.type.VectorType();

    if (IsStruct(vectortype)) {
      code += "// IsStruct " + NativeType(field.value.type.VectorType()) + "\n";
    } else {
      std::string getter = GenGetter(field.value.type);
      GenReceiver(struct_def, code_ptr);
      code += " " + GoIdentity(field.name);
      code += "() [" + NumToString(field.value.type.fixed_length) + "]";
      code += NativeType(field.value.type.VectorType()) + " {\n";

      code +=
          "\tresult := [" + NumToString(field.value.type.fixed_length) + "]";
      code += NativeType(field.value.type.VectorType()) + "{}\n";

      code += "\ta := flatbuffers.UOffsetT(" + NumToString(field.value.offset) +
              ")\n";
      code += "\tfor j := 0; j < " + NumToString(field.value.type.fixed_length);
      code += "; j++ {\n";
      code += "\t\t\tresult[j] = " +
              CastToEnum(field.value.type,
                         GenGetter(field.value.type.VectorType()) +
                             "(a + flatbuffers.UOffsetT(j*" +
                             NumToString(InlineSize(vectortype)) + "))");
      code += "\n\t}\n";
      code += "\treturn result\n";
      code += "}\n\n";
    }
  }
  // Get the value of a table's scalar.
  void GetFixedScalarFieldOfTable(const StructDef &struct_def,
                                  const FieldDef &field,
                                  std::string *code_ptr) {
    std::string &code = *code_ptr;
    std::string getter = GenGetter(field.value.type);
    GenReceiver(struct_def, code_ptr);
    code += " " + GoIdentity(field.name);
    code += "() " + TypeName(field) + " ";
    code += OffsetPrefix(field) + "\t\treturn ";
    code += CastToEnum(field.value.type, getter + "(o + rcv._tab.Pos)");
    code += "\n\t}\n";
    code += "\treturn " + GenConstant(field) + "\n";
    code += "}\n\n";
  }

  // Mutate the value of a struct's scalar.
  void MutateScalarFieldOfStruct(const StructDef &struct_def,
                                 const FieldDef &field, std::string *code_ptr) {
    std::string &code = *code_ptr;
    std::string type = GoIdentity(GenTypeBasic(field.value.type));
    std::string setter = "rcv._tab.Mutate" + type;
    GenReceiver(struct_def, code_ptr);
    code += " Mutate" + GoIdentity(field.name);
    code += "(n " + TypeName(field) + ") bool {\n\treturn " + setter;
    code += "(rcv._tab.Pos + flatbuffers.UOffsetT(";
    code += NumToString(field.value.offset) + "), ";
    code += CastToBaseType(field.value.type, "n") + ")\n}\n\n";
  }

  // Mutate the value of a table's scalar.
  void MutateScalarFieldOfTable(const StructDef &struct_def,
                                const FieldDef &field, std::string *code_ptr) {
    std::string &code = *code_ptr;
    std::string type = GoIdentity(GenTypeBasic(field.value.type));
    std::string setter = "rcv._tab.Mutate" + type + "Slot";
    GenReceiver(struct_def, code_ptr);
    code += " Mutate" + GoIdentity(field.name);
    code += "(n " + TypeName(field) + ") bool {\n\treturn ";
    code += setter + "(" + NumToString(field.value.offset) + ", ";
    code += CastToBaseType(field.value.type, "n") + ")\n";
    code += "}\n\n";
  }

  // Mutate an element of a vector of scalars.
  void MutateElementOfVectorOfNonStruct(const StructDef &struct_def,
                                        const FieldDef &field,
                                        std::string *code_ptr) {
    std::string &code = *code_ptr;
    auto vectortype = field.value.type.VectorType();
    std::string type = GoIdentity(GenTypeBasic(vectortype));
    std::string setter = "rcv._tab.Mutate" + type;
    GenReceiver(struct_def, code_ptr);
    code += " Mutate" + GoIdentity(field.name);
    code += "(j int, n " + TypeName(field) + ") bool ";
    code += OffsetPrefix(field);
    code += "\t\ta := rcv._tab.Vector(o)\n";
    code += "\t\treturn " + setter + "(";
    code += "a + flatbuffers.UOffsetT(j*";
    code += NumToString(InlineSize(vectortype)) + "), ";
    code += CastToBaseType(vectortype, "n") + ")\n";
    code += "\t}\n";
    code += "\treturn false\n";
    code += "}\n\n";
  }

  // Generate a struct field setter, conditioned on its child type(s).
  void GenStructMutator(const StructDef &struct_def, const FieldDef &field,
                        std::string *code_ptr) {
    GenComment(field.doc_comment, code_ptr, nullptr, "");
    if (IsScalar(field.value.type.base_type)) {
      if (struct_def.fixed) {
        MutateScalarFieldOfStruct(struct_def, field, code_ptr);
      } else {
        MutateScalarFieldOfTable(struct_def, field, code_ptr);
      }
    } else if (field.value.type.base_type == BASE_TYPE_VECTOR) {
      if (IsScalar(field.value.type.element)) {
        MutateElementOfVectorOfNonStruct(struct_def, field, code_ptr);
      }
    }
  }

  // Generate table constructors, conditioned on its members' types.
  void GenTableBuilders(const StructDef &struct_def, std::string *code_ptr) {
    // builder start
    GetStartOfTable(struct_def, code_ptr);

    // each field builder
    for (auto it = struct_def.fields.vec.begin();
         it != struct_def.fields.vec.end(); ++it) {
      auto &field = **it;

      if (field.deprecated) continue;

      auto offset = it - struct_def.fields.vec.begin();

      // vector field
      if (field.value.type.base_type == BASE_TYPE_VECTOR) {
        BuildVectorOfTable(struct_def, field, code_ptr);
      }
      // field builder
      BuildFieldOfTable(struct_def, field, offset, code_ptr);
    }
    // builder end
    GetEndOffsetOnTable(struct_def, code_ptr);
  }

  // Generate struct or table methods.
  void GenStruct(const StructDef &struct_def, std::string *code_ptr) {
    if (struct_def.generated) return;
    // bypass nil table ( zero field in a table )
    if (struct_def.fields.vec.size() == 0) return;

    cur_name_space_ = struct_def.defined_namespace;
    // if namespace is missing, use root_type as namespace
    if (cur_name_space_->components.empty()) {
      if (parser_.root_struct_def_) {
        cur_name_space_->components.push_back(parser_.root_struct_def_->name);
      }
      // TODO: tsingson: if both namespace / root_type is missing, need
      // somewhere throw exception
    }

    GenComment(struct_def.doc_comment, code_ptr, nullptr);

    // generate native struct / table object and api
    if (generate_object_based_api) { GenNativeStruct(struct_def, code_ptr); }

    BeginClass(struct_def, code_ptr);

    if (!struct_def.fixed) {
      // Generate accessor for the table
      NewRootTypeFromBuffer(struct_def, code_ptr);
    } else {
      // generate accessor for the struct
      // TODO: tsingson, plan to support StructBuffers
      NewStructTypeFromBuffer(struct_def, code_ptr);
    }

    // Generate the Init method that sets the field in a pre-existing
    // accessor object. This is to allow object reuse.
    InitializeExisting(struct_def, code_ptr);
    // Generate _tab accessor
    GenTableAccessor(struct_def, code_ptr);

    // Generate fields accessors inside struct / table
    for (auto it = struct_def.fields.vec.begin();
         it != struct_def.fields.vec.end(); ++it) {
      auto &field = **it;

      // TODO: tsingson, add accessor for fixed length struct array

      if (field.deprecated) continue;

      GenStructAccessor(struct_def, field, code_ptr);

      if (mutable_buffer) { GenStructMutator(struct_def, field, code_ptr); }
    }  //

    // Generate builders for struct / table
    if (struct_def.fixed) {  // TODO: tsingson, add stack for struct_def
      // create struct constructor as one via go native object parameter
      // example:
      //
      GenStructBuilder(struct_def, code_ptr);

      // create a struct builder as parameter group
      // example:
      //
      //      GenFixedStructBuilder(struct_def, code_ptr);

    } else {
      // Create a set of functions that allow table construction.
      GenTableBuilders(struct_def, code_ptr);
    }
    //
  }

  // Create a struct with fixed length array field
  void GenFixedStructBuilder(const StructDef &struct_def,
                             std::string *code_ptr) {
    // struct builder declaration
    std::string &code = *code_ptr;
    std::string nameprefix = "";
    // stack push/pop
    std::vector<FieldDef> *temp_field_ = nullptr;
    std::vector<std::vector<FieldDef> *> fixed_field_;
    std::vector<FieldDef> *temp_ = nullptr;
    // stack loop
    for (auto it = struct_def.fields.vec.begin();
         it != struct_def.fields.vec.end(); ++it) {
      auto &field = **it;
      // begin() handle
      if (it == struct_def.fields.vec.begin()) {
        temp_ = new std::vector<FieldDef>;
      }
      // if field is struct or array , split to new group
      if ((IsStruct(field.value.type)) || (IsArray(field.value.type))) {
        if ((it != struct_def.fields.vec.end()) && (!(*temp_).empty())) {
          temp_field_ = temp_;
          fixed_field_.push_back(temp_field_);
          temp_ = new std::vector<FieldDef>;
        }
        (*temp_).push_back(field);
        temp_field_ = temp_;
        fixed_field_.push_back(temp_field_);
        temp_ = new std::vector<FieldDef>;
      } else {
        (*temp_).push_back(field);
      }
      // end() handle
      if ((it == struct_def.fields.vec.end() - 1) && (!(*temp_).empty())) {
        temp_field_ = temp_;
        fixed_field_.push_back(temp_field_);
      }
    }
    // stack loop end

    // builder start
    code += "func Create" + GoIdentity(struct_def.name);
    code += "StepStart(builder *flatbuffers.Builder){\n";
    code += "\tbuilder.Prep(" + NumToString(struct_def.minalign) + ", ";
    code += NumToString(struct_def.bytesize) + ")\n";
    code += "}\n\n";

    // handle builder
    for (auto it = fixed_field_.begin(); it != fixed_field_.end(); ++it) {
      auto &item = **it;
      auto offset = it - fixed_field_.begin() + 1;

      if (code.substr(code.length() - 2) != "\n\n") {
        // a previous mutate has not put an extra new line
        code += "\n";
      }
      code += "func Create" + GoIdentity(struct_def.name);
      code += "Step" + NumToString(offset) + "(builder *flatbuffers.Builder";

      //  parameter args
      for (auto j = item.begin(); j != item.end(); ++j) {
        auto &field = *j;
        if (IsStruct(field.value.type)) {
          // Generate arguments for a struct inside a struct. To ensure names
          // don't clash, and to make it obvious these arguments are
          // constructing a nested struct, prefix the name with the field name.
          StructBuilderArgs(*field.value.type.struct_def,
                            (nameprefix + (field.name + "_")).c_str(),
                            code_ptr);
        } else if (IsArray(field.value.type)) {
          std::string &code = *code_ptr;
          code += std::string(",\n\t") + nameprefix;
          code += GoIdentity(field.name, false);
          code += " [" + NumToString(field.value.type.fixed_length) + "]";
          code += NativeType(field.value.type.VectorType());
        } else {
          std::string &code = *code_ptr;
          code += std::string(",\n\t") + nameprefix;
          code += GoIdentity(field.name, false);
          code += " " + TypeName(field);
        }
      }
      code += ") {\n";
      // builder body
      for (auto j = item.begin(); j != item.end(); ++j) {
        auto &field = *j;

        if (field.padding) {
          code += "\tbuilder.Pad(" + NumToString(field.padding) + ")\n";
          continue;
        }
        if (IsStruct(field.value.type)) {
          StructBuilderBody(*field.value.type.struct_def,
                            (nameprefix + (field.name + "_")).c_str(),
                            code_ptr);
        } else if (IsArray(field.value.type)) {
          if (IsStruct(field.value.type.VectorType())) {
            code += "\t// " + NativeType(field.value.type.VectorType()) + "\n";
          } else {
            //        code += "// fixed array field builder\n";
            code += "\tfor j := " + NumToString(field.value.type.fixed_length);
            code += "; j == 0; j-- {\n";
            code += "\t\tbuilder.Prepend" +
                    NativeType(field.value.type.VectorType()) + "(";
            code += GoIdentity(field.name, false);
            code += "[j])\n";
            code += "\t}\n";
          }
        } else {
          code += "\tbuilder.Prepend" + GenMethod(field) + "(";
          code += CastToBaseType(field.value.type,
                                 nameprefix + GoIdentity(field.name, false)) +
                  ")\n";
        }
      }
      code += "}\n\n";
      //      code += "//-------------------- " + NumToString(offset) + "\n";
    }
    // builder start
    code += "func Create" + GoIdentity(struct_def.name);
    code += "StepEnd(builder *flatbuffers.Builder){\n";
    code += "\treturn builder.Offset()\n";
    code += "}\n\n";
  }

  // generate native go object for struct
  void GenNativeStruct(const StructDef &struct_def, std::string *code_ptr) {
    std::string &code = *code_ptr;

    code += "// " + NativeName(struct_def) + " native go object\n";
    code += "type " + NativeName(struct_def) + " struct {\n";
    for (auto it = struct_def.fields.vec.begin();
         it != struct_def.fields.vec.end(); ++it) {
      const FieldDef &field = **it;

      if (field.deprecated) continue;

      // pass union type field
      if (IsScalar(field.value.type.base_type) &&
          field.value.type.enum_def != nullptr &&
          field.value.type.enum_def->is_union)
        continue;

      // pass vector of unions type
      auto vectortype = field.value.type.VectorType();  // get element's type

      // is unions vector's type , bypass
      // do not need a native go field for
      if ((field.value.type.base_type == BASE_TYPE_VECTOR) ||
          (field.value.type.base_type == BASE_TYPE_ARRAY)) {
        //
        if (IsScalar(vectortype.base_type) &&
            (vectortype.enum_def != nullptr) && (vectortype.enum_def->is_union))
          continue;
      }

      code += "\t" + GoIdentity(field.name) + " " +
              NativeType(field.value.type) + "\n";
    }  //

    code += "}\n\n";

    // generate native object api
    if (!struct_def.fixed) {
      // table
      GenNativeTablePack(struct_def, code_ptr);
      GenNativeTableUnPack(struct_def, code_ptr);
    } else {
      // struct
      GenNativeStructPack(struct_def, code_ptr);
      GenNativeStructUnPack(struct_def, code_ptr);
    }
  }
  // generate native go object for union
  void GenNativeUnion(const EnumDef &enum_def, std::string *code_ptr) {
    std::string &code = *code_ptr;
    code += "type " + NativeName(enum_def) + " struct {\n";
    code += "\tType  " + enum_def.name + "\n";
    code += "\tValue interface{}\n";
    code += "}\n\n";
  }

  // native union object pack
  void GenNativeUnionPack(const EnumDef &enum_def, std::string *code_ptr) {
    std::string &code = *code_ptr;
    code += "func (t *" + NativeName(enum_def) +
            ") Pack(builder *flatbuffers.Builder) flatbuffers.UOffsetT {\n";
    code += "\tif t == nil {\n\t\treturn 0\n\t}\n";

    code += "\tswitch t.Type {\n";
    for (auto it2 = enum_def.Vals().begin(); it2 != enum_def.Vals().end();
         ++it2) {
      const EnumVal &ev = **it2;
      if (ev.IsZero()) continue;
      if (ev.union_type.base_type == BASE_TYPE_STRING) {
        code += "\tcase " + enum_def.name + ev.name + ":\n";
        code += "\t\treturn builder.CreateString(t.Value.(" +
                NativeType(ev.union_type) + "))\n";
        continue;
      }

      code += "\tcase " + enum_def.name + ev.name + ":\n";
      code += "\t\treturn t.Value.(" + NativeType(ev.union_type) +
              ").Pack(builder)\n";
    }
    code += "\t}\n";
    code += "\treturn 0\n";
    code += "}\n\n";
  }

  // native union unpack
  void GenNativeUnionUnPack(const EnumDef &enum_def, std::string *code_ptr) {
    std::string &code = *code_ptr;
    code += "// UnPack use for single union field\n";
    code +=
        "func (rcv " + enum_def.name + ") UnPack(table flatbuffers.Table) *";
    code += NativeName(enum_def) + " {\n";
    code += "\tswitch rcv {\n";

    for (auto it2 = enum_def.Vals().begin(); it2 != enum_def.Vals().end();
         ++it2) {
      const EnumVal &ev = **it2;
      if (ev.IsZero()) continue;
      if (ev.union_type.base_type == BASE_TYPE_STRING) {
        code += "\tcase " + enum_def.name + ev.name + ":\n";
        code += "\t\tx := string(table.StringsVector(table.Pos))\n";
        code +=
            "\t\treturn &" + WrapInNameSpaceAndTrack(enum_def.defined_namespace,
                                                     NativeName(enum_def));
        code += "{Type: " + enum_def.name + ev.name + ", Value: x}\n";

      } else if ((ev.union_type.base_type == BASE_TYPE_STRUCT) &&
                 (ev.union_type.struct_def->fixed)) {  // struct
        code += "\tcase " + enum_def.name + ev.name + ":\n";
        code += "\t\tx := ";
        code +=
            WrapInNameSpacePrefix(ev.union_type.struct_def->defined_namespace);
        code += "GetStructAs" + ev.union_type.struct_def->name;
        code += "(&table)\n";
        code +=
            "\t\treturn &" + WrapInNameSpaceAndTrack(enum_def.defined_namespace,
                                                     NativeName(enum_def));
        code += "{Type: " + enum_def.name + ev.name + ", Value: x.UnPack()}\n";

      } else {  // table
        code += "\tcase " + enum_def.name + ev.name + ":\n";
        code += "\t\tx := ";
        code +=
            WrapInNameSpacePrefix(ev.union_type.struct_def->defined_namespace);
        code += "GetTableAs" + ev.union_type.struct_def->name;
        code += "(&table)\n";
        code +=
            "\t\treturn &" + WrapInNameSpaceAndTrack(enum_def.defined_namespace,
                                                     NativeName(enum_def));
        code += "{Type: " + enum_def.name + ev.name + ", Value: x.UnPack()}\n";
      }
    }
    code += "\t}\n";
    code += "\treturn nil\n";
    code += "}\n\n";
  }

  // unpack vector of unions
  void GenNativeUnionUnPackIn(const EnumDef &enum_def, std::string *code_ptr) {
    std::string &code = *code_ptr;

    code += "// UnPackVector use for vector of unions\n";
    code += "func (rcv " + enum_def.name +
            ") UnPackVector(table flatbuffers.Table) *";
    code += NativeName(enum_def) + " {\n";
    code += "\tswitch rcv {\n";

    for (auto it2 = enum_def.Vals().begin(); it2 != enum_def.Vals().end();
         ++it2) {
      const EnumVal &ev = **it2;
      if (ev.IsZero()) continue;
      if (ev.union_type.base_type == BASE_TYPE_STRING) {
        code += "\tcase " + enum_def.name + ev.name + ":\n";
        code += "\t\tx := \"\"\n";
        code += "\t\tb := table.ByteVector(table.Pos)\n";
        code += "\t\tif b != nil {\n";
        code += "\t\t\tx = string(b)\n";
        code += "\t\t}\n";
        code +=
            "\t\treturn &" + WrapInNameSpaceAndTrack(enum_def.defined_namespace,
                                                     NativeName(enum_def));
        code += "{Type: " + enum_def.name + ev.name + ", Value: x}\n";

      } else if ((ev.union_type.base_type == BASE_TYPE_STRUCT) &&
                 (ev.union_type.struct_def->fixed)) {
        code += "\tcase " + enum_def.name + ev.name + ":\n";
        code += "\t\tx := ";
        code +=
            WrapInNameSpacePrefix(ev.union_type.struct_def->defined_namespace);
        code += "GetStructVectorAs" + ev.union_type.struct_def->name;
        code += "(&table)\n";
        code +=
            "\t\treturn &" + WrapInNameSpaceAndTrack(enum_def.defined_namespace,
                                                     NativeName(enum_def));
        code += "{Type: " + enum_def.name + ev.name + ", Value: x.UnPack()}\n";

      } else {
        code += "\tcase " + enum_def.name + ev.name + ":\n";
        code += "\t\tx := ";
        code +=
            WrapInNameSpacePrefix(ev.union_type.struct_def->defined_namespace);
        code += "GetTableVectorAs" + ev.union_type.struct_def->name;
        code += "(&table)\n";

        code +=
            "\t\treturn &" + WrapInNameSpaceAndTrack(enum_def.defined_namespace,
                                                     NativeName(enum_def));
        code += "{Type: " + enum_def.name + ev.name + ", Value: x.UnPack()}\n";
      }
    }
    code += "\t}\n";
    code += "\treturn nil\n";
    code += "}\n\n";
  }

  // generate Pack function for native table
  void GenNativeTablePack(const StructDef &struct_def, std::string *code_ptr) {
    std::string &code = *code_ptr;

    code += "// " + NativeName(struct_def) + " object pack function\n";

    code += "func (t *" + NativeName(struct_def) +
            ") Pack(builder *flatbuffers.Builder) flatbuffers.UOffsetT {\n";
    code += "\tif t == nil {\n\t\treturn 0\n\t}\n";

    // prepare vector field into flatbuffers
    // vector field include:
    // 1. single vector, like  string / byte slice / table
    // 2. union ( union's value field is vector, union's type field is inline
    // scalar)
    // 3. array of enum / scalar / string / table / struct
    // 4. unions vector ( array of unions )

    for (auto it = struct_def.fields.vec.begin();
         it != struct_def.fields.vec.end(); ++it) {
      const FieldDef &field = **it;

      if (field.deprecated) continue;
      // pass scalar , it's inline
      if (IsScalar(field.value.type.base_type)) continue;
      // pass vector of unions type
      auto vectortype = field.value.type.VectorType();  // get element's type

      // bypass unions vector type file , handle inside unions vector field
      //
      // for single union field in table IDL, it's two slot:
      //  union's type field
      //  union's value field
      //
      // for unions vector field in table IDL, it's two slot:
      // unions vector's type field , is a field for union type array
      // unions vector's value field , is a field for union value array
      //
      // so, bypass a unions vector's type field,
      // generated code in unions vector's value field at same time
      if ((field.value.type.base_type == BASE_TYPE_VECTOR) ||
          (field.value.type.base_type == BASE_TYPE_ARRAY)) {
        if (IsScalar(vectortype.base_type) && vectortype.enum_def != nullptr &&
            vectortype.enum_def->is_union)
          continue;
      }

      // UOffset for vector field in table IDL
      std::string offset = GoIdentity(field.name, false) + "Offset";

      // handle string vector field
      if (field.value.type.base_type == BASE_TYPE_STRING) {
        code += "\t" + offset + " := flatbuffers.UOffsetT(0)\n";
        code += "\tif len(t." + GoIdentity(field.name) + ") > 0 {\n";
        code += "\t\t" + offset + " = builder.CreateString(t." +
                GoIdentity(field.name) + ")\n\t}\n";
      }
      // handle byte slice vector field
      else if (field.value.type.base_type == BASE_TYPE_VECTOR &&
               field.value.type.element == BASE_TYPE_UCHAR &&
               field.value.type.enum_def == nullptr) {
        code += "\t" + offset + " := flatbuffers.UOffsetT(0)\n";
        code += "\tif t." + GoIdentity(field.name) + " != nil {\n";
        code += "\t\t" + offset + " = builder.CreateByteString(t." +
                GoIdentity(field.name) + ")\n";
        code += "\t}\n";
      }
      // handle vector field
      else if (field.value.type.base_type == BASE_TYPE_VECTOR) {
        // vector of strings ( string array )
        if (field.value.type.element == BASE_TYPE_STRING) {
          code += "\t" + offset + " := flatbuffers.UOffsetT(0)\n";
          code += "\tif t." + GoIdentity(field.name) + " != nil {\n";

          code += "\t\t" + offset + " = builder.StringsVector(t." +
                  GoIdentity(field.name) + "...)\n";
          code += "\t}\n";
        }
        // handle unions vector field ( array of unions )
        else if (field.value.type.element == BASE_TYPE_UNION) {
          genUnionsVectorPack(struct_def, field, code_ptr);
        }
        //  table's array field
        else if (field.value.type.element == BASE_TYPE_STRUCT &&
                 !field.value.type.struct_def->fixed) {
          genTableArrayPack(struct_def, field, code_ptr);
        }
        // scalar's array field
        else if (IsScalar(field.value.type.element)) {
          genScalarArrayPack(struct_def, field, code_ptr);
        }
        // struct's array field
        else if (field.value.type.element == BASE_TYPE_STRUCT &&
                 field.value.type.struct_def->fixed) {
          genStructArrayPack(struct_def, field, code_ptr);
        }
        // end vector
      }
      // struct field
      else if (field.value.type.base_type == BASE_TYPE_STRUCT) {
        // pass single struct , it's inline
        if (field.value.type.struct_def->fixed) continue;
        // table
        code += "\t" + offset + " := t." + GoIdentity(field.name) +
                ".Pack(builder)\n";
      }  // single union field
      else if (field.value.type.base_type == BASE_TYPE_UNION) {
        code += "\t" + offset + " := t." + GoIdentity(field.name) +
                ".Pack(builder)\n";
        code += "\t\n";
      } else {
        FLATBUFFERS_ASSERT(0);
      }
    }

    // start builder
    code += "\n\t" + struct_def.name + "Start(builder)\n";

    for (auto it = struct_def.fields.vec.begin();
         it != struct_def.fields.vec.end(); ++it) {
      const FieldDef &field = **it;
      auto &field_type = field.value.type;
      auto vectortype = field.value.type.VectorType();  // get element's type

      if (field.deprecated) continue;

      // bypass unions vector's type field, handle with unions vector's value
      // field
      if ((field.value.type.base_type == BASE_TYPE_VECTOR) ||
          (field.value.type.base_type == BASE_TYPE_ARRAY)) {
        if (IsScalar(vectortype.base_type) && vectortype.enum_def != nullptr &&
            vectortype.enum_def->is_union)
          continue;
      }

      std::string offset = GoIdentity(field.name, false) + "Offset";

      // build scalar field
      if (IsScalar(field.value.type.base_type)) {
        if (field.value.type.enum_def == nullptr ||
            !field.value.type.enum_def->is_union) {
          code += "\t" + struct_def.name + "Add" + GoIdentity(field.name) +
                  "(builder, t." + GoIdentity(field.name) + ")\n";
        }
      }  // build vector field
      else {
        // vector array
        if ((field_type.base_type == BASE_TYPE_VECTOR) ||
            (field_type.base_type == BASE_TYPE_ARRAY)) {
          // unions vector (  array of union )
          if (vectortype.base_type == BASE_TYPE_UNION) {
            //  code += "\t\t\t// union in array ----------- \n";
            std::string offset_type =
                GoIdentity(field.name, false) + "TypeOffset";
            code += "\t" + struct_def.name + "Add" + GoIdentity(field.name) +
                    "Type(builder, " + offset_type + ")\n";

            code += "\t" + struct_def.name + "Add" + GoIdentity(field.name) +
                    "(builder, " + offset + ")\n";

          }  // another vector array
          else {
            code += "\t" + struct_def.name + "Add" + GoIdentity(field.name) +
                    "(builder, " + offset + ")\n";
          }
        }

        // single vector field
        // struct field
        else if (field.value.type.base_type == BASE_TYPE_STRUCT &&
                 field.value.type.struct_def->fixed) {
          code += "\t" + offset + " := t." + GoIdentity(field.name) +
                  ".Pack(builder)\n";
          code += "\t" + struct_def.name + "Add" + GoIdentity(field.name) +
                  "(builder, " + offset + ")\n";
        }  // single union field
        else if (field.value.type.enum_def != nullptr &&
                 field.value.type.enum_def->is_union) {
          code += "\tif t." + GoIdentity(field.name) + " != nil {\n";
          code += "\t\t" + struct_def.name + "Add" +
                  GoIdentity(field.name + UnionTypeFieldSuffix()) +
                  "(builder, t." + GoIdentity(field.name) + ".Type)\n";
          code += "\t}\n";
          code += "\t" + struct_def.name + "Add" + GoIdentity(field.name) +
                  "(builder, " + offset + ")\n";
        }  // other vector field
        else {
          code += "\t" + struct_def.name + "Add" + GoIdentity(field.name) +
                  "(builder, " + offset + ")\n";
        }
      }
    }  // end for
    // end builder
    code += "\treturn " + struct_def.name + "End(builder)\n";
    code += "}\n\n";
  }

  // generate scalar array pack func ( array of scalar )
  void genScalarArrayPack(const StructDef &struct_def, const FieldDef &field,
                          std::string *code_ptr) {
    std::string &code = *code_ptr;

    std::string offset = GoIdentity(field.name, false) + "Offset";
    std::string length = GoIdentity(field.name, false) + "Length";
    std::string offsets = GoIdentity(field.name, false) + "Offsets";
    code += "\t" + offset + " := flatbuffers.UOffsetT(0)\n";
    code += "\tif t." + GoIdentity(field.name) + " != nil {\n";

    code += "\t\t" + length + " := len(t." + GoIdentity(field.name) + ")\n";

    code += "\t\t" + struct_def.name + "Start" + GoIdentity(field.name) +
            "Vector(builder, " + length + ")\n";
    code += "\t\tfor j := " + length + " - 1; j >= 0; j-- {\n";
    //     code += "\t// scalar array \n";
    code += "\t\t\tbuilder.Prepend" +
            GoIdentity(GenTypeBasic(field.value.type.VectorType())) + "(" +
            CastToBaseType(field.value.type.VectorType(),
                           "t." + GoIdentity(field.name) + "[j]") +
            ")\n";
    //     code += "\t// end vector \n";
    code += "\t\t}\n";
    code += "\t\t" + offset + " = " + struct_def.name + "End" +
            GoIdentity(field.name) + "Vector(builder, " + length + ")\n";
    code += "\t}\n";
  }
  // generate struct array pack func ( array of scalar )
  void genStructArrayPack(const StructDef &struct_def, const FieldDef &field,
                          std::string *code_ptr) {
    std::string &code = *code_ptr;

    std::string offset = GoIdentity(field.name, false) + "Offset";
    std::string length = GoIdentity(field.name, false) + "Length";
    std::string offsets = GoIdentity(field.name, false) + "Offsets";

    code += "\t" + offset + " := flatbuffers.UOffsetT(0)\n";
    code += "\tif t." + GoIdentity(field.name) + " != nil {\n";

    code += "\t\t" + length + " := len(t." + GoIdentity(field.name) + ")\n";
    // vector start
    code += "\t\t" + struct_def.name + "Start" + GoIdentity(field.name) +
            "Vector(builder, " + length + ")\n";
    //
    code += "\t\tfor j := " + length + " - 1; j >= 0; j-- {\n";
    code += "\t\t\tt." + GoIdentity(field.name) + "[j].Pack(builder)\n";
    code += "\t\t}\n";
    // vector end
    code += "\t\t" + offset + " = " + struct_def.name + "End" +
            GoIdentity(field.name) + "Vector(builder, " + length + ")\n";
    code += "\t}\n";
  }
  // generate table array pack func ( array of tables )
  void genTableArrayPack(const StructDef &struct_def, const FieldDef &field,
                         std::string *code_ptr) {
    std::string &code = *code_ptr;

    std::string offset = GoIdentity(field.name, false) + "Offset";
    std::string length = GoIdentity(field.name, false) + "Length";
    std::string offsets = GoIdentity(field.name, false) + "Offsets";
    code += "\t// vector of tables \n";

    code += "\t" + offset + " := flatbuffers.UOffsetT(0)\n";
    code += "\tif t." + GoIdentity(field.name) + " != nil {\n";

    code += "\t\t" + length + " := len(t." + GoIdentity(field.name) + ")\n";

    code +=
        "\t\t" + offsets + " := make([]flatbuffers.UOffsetT, " + length + ")\n";
    code += "\t\tfor j := " + length + " - 1; j >= 0; j-- {\n";
    code += "\t\t\t" + offsets + "[j] = t." + GoIdentity(field.name) +
            "[j].Pack(builder)\n";
    code += "\t\t}\n";
    // handle UOffsets
    // start vector
    code += "\t\t" + struct_def.name + "Start" + GoIdentity(field.name) +
            "Vector(builder, " + length + ")\n";

    code += "\t\tfor j := " + length + " - 1; j >= 0; j-- {\n";
    code += "\t\t\tbuilder.PrependUOffsetT(" + offsets + "[j])\n";
    code += "\t\t}\n";
    // end vector
    code += "\t\t" + offset + " = " + struct_def.name + "End" +
            GoIdentity(field.name) + "Vector(builder, " + length + ")\n";
    code += "\t}\n";
  }
  // generate unions vector pack func ( array of unions )
  void genUnionsVectorPack(const StructDef &struct_def, const FieldDef &field,
                           std::string *code_ptr) {
    std::string &code = *code_ptr;

    //  auto vectortype = field.value.type.VectorType();  // get element's type
    // UOffset for vector field in table IDL
    std::string offset = GoIdentity(field.name, false) + "Offset";
    std::string length = GoIdentity(field.name, false) + "Length";
    std::string offsets = GoIdentity(field.name, false) + "Offsets";
    code += "\t// vector of unions \n";

    // for unions vector field in table IDL, it's two slot:
    // unions vector's type field , is a field for union type array
    // unions vector's value field , is a field for union value array

    // UOffset for unions vector's type field
    std::string offset_type = GoIdentity(field.name, false) + "TypeOffset";

    // generate go code to initial type /value 's UOffset
    code += "\t" + offset + " := flatbuffers.UOffsetT(0)\n";
    code += "\t" + offset_type + " := flatbuffers.UOffsetT(0)\n";

    code += "\tif t." + GoIdentity(field.name) + " != nil {\n";
    code += "\t\t" + length + " := len(t." + GoIdentity(field.name) + ")\n";

    // unions vector's type field to fbs
    code += "\t\t" + struct_def.name + "Start";
    code += GoIdentity(field.name);
    code += "TypeVector(builder, " + length + ")\n";
    code += "\t\tfor j := " + length + " - 1; j >= 0; j-- {\n";
    code += "\t\t\tbuilder.PrependByte(byte(t." + GoIdentity(field.name) +
            "[j].Type))\n";
    code += "\t\t}\n";
    code += "\t\t" + offset_type + " = " + struct_def.name + "End";
    code += GoIdentity(field.name);
    code += "TypeVector(builder, " + length + ")\n\n";
    // unions vector's type field UOffset

    // unions vector's value field
    code += "\t\t// vector array\n";
    code +=
        "\t\t" + offsets + " := make([]flatbuffers.UOffsetT, " + length + ")\n";

    code += "\t\tfor j := " + length + " - 1; j >= 0; j-- {\n";
    code += "\t\t\t" + offsets + "[j] = t." + GoIdentity(field.name) +
            "[j].Pack(builder)\n";
    code += "\t\t}\n";

    // start vector
    code += "\t\t" + struct_def.name + "Start";
    code += GoIdentity(field.name);
    code += "Vector(builder, " + length + ")\n";
    //
    code += "\t\tfor j := " + length + " - 1; j >= 0; j-- {\n";
    code += "\t\t\tbuilder.PrependUOffsetT(" + offsets + "[j])\n";
    code += "\t\t}\n";
    // end vector
    code += "\t\t" + offset + " = " + struct_def.name + "End";
    code += GoIdentity(field.name);
    code += "Vector(builder, " + length + ")\n";
    code += "\t}\n\n";
    //
  }

  // object api UnpackTo / Unpack
  void GenNativeTableUnPack(const StructDef &struct_def,
                            std::string *code_ptr) {
    std::string &code = *code_ptr;

    code += "// " + NativeName(struct_def) + " object unpack function\n";

    code += "func (rcv *" + struct_def.name + ") UnPackTo(t *" +
            NativeName(struct_def) + ") {\n";
    for (auto it = struct_def.fields.vec.begin();
         it != struct_def.fields.vec.end(); ++it) {
      const FieldDef &field = **it;

      if (field.deprecated) continue;
      // pass vector of unions type
      auto vectortype = field.value.type.VectorType();  // get element's type

      if ((field.value.type.base_type == BASE_TYPE_VECTOR) ||
          (field.value.type.base_type == BASE_TYPE_ARRAY)) {
        if (IsScalar(vectortype.base_type) && vectortype.enum_def != nullptr &&
            vectortype.enum_def->is_union)
          continue;
      }

      std::string field_name_camel = GoIdentity(field.name);
      std::string length = GoIdentity(field.name, false) + "Length";
      if (IsScalar(field.value.type.base_type)) {
        if (field.value.type.enum_def != nullptr &&
            field.value.type.enum_def->is_union)
          continue;
        code +=
            "\tt." + field_name_camel + " = rcv." + field_name_camel + "()\n";
      }  //
      else if (field.value.type.base_type == BASE_TYPE_STRING) {
        code += "\tt." + field_name_camel + " = string(rcv." +
                field_name_camel + "())\n";
      }  //  field:[ubyte];
      else if (field.value.type.base_type == BASE_TYPE_VECTOR &&
               field.value.type.element == BASE_TYPE_UCHAR &&
               field.value.type.enum_def == nullptr) {
        code += "\tt." + field_name_camel + " = rcv." + field_name_camel +
                "Bytes()\n";
      }  //
      else if (field.value.type.base_type == BASE_TYPE_VECTOR) {
        //
        code += "\t" + length + " := rcv." + field_name_camel + "Length()\n";
        code += "\tt." + field_name_camel + " = make(" +
                NativeType(field.value.type) + ", " + length + ")\n";

        code += "\tfor j := 0; j < " + length + "; j++ {\n";

        // enum or ubyte field in array
        if (field.value.type.element == BASE_TYPE_UCHAR) {
          code += "\t\tt." + field_name_camel + "[j] = ";
          code += TypeName(field) + "(";
          code += "rcv." + field_name_camel + "Bytes()[j])";

        }  // struct array
        else if (field.value.type.element == BASE_TYPE_STRUCT) {
          code +=
              "\t\tx := " + WrapInNameSpacePrefix(
                                field.value.type.struct_def->defined_namespace);
          code += field.value.type.struct_def->name + "{}\n";
          code += "\t\trcv." + field_name_camel + "(&x, j)\n";
        }  // scalar array
        else if (IsScalar(field.value.type.element)) {
          code += "\t\tt." + field_name_camel + "[j] = ";
          code += "rcv." + field_name_camel + "(j)";

        }  // string array
        else if (field.value.type.element == BASE_TYPE_STRING) {
          code += "\t\tt." + field_name_camel + "[j] = ";
          code += "string(rcv." + field_name_camel + "(j))\n";
        }  // struct field
        else if (field.value.type.element == BASE_TYPE_STRUCT) {
          code += "\t\tt." + field_name_camel + "[j] = ";
          code += "x.UnPack()\n";
        }  // support vector of unions
        else if (field.value.type.element == BASE_TYPE_UNION) {
          code += "\t\t" + field_name_camel + "Type := rcv." +
                  field_name_camel + "Type(j)\n";
          code += "\t\t" + field_name_camel + "Table := flatbuffers.Table{}\n";
          code += "\t\tif rcv." + field_name_camel + "(j, &" +
                  field_name_camel + "Table) {\n";
          code += "\t\t\tt." + field_name_camel + "[j] = " + field_name_camel +
                  "Type.UnPackVector(" + field_name_camel + "Table)\n";
          code += "\t\t}\n";

        } else {
          FLATBUFFERS_ASSERT(0);
        }
        code += "\t}\n";
      }  // struct or table
      else if (field.value.type.base_type == BASE_TYPE_STRUCT) {
        code += "\tt." + field_name_camel + " = rcv." + field_name_camel +
                "(nil).UnPack()\n";
      }  // single union
      else if (field.value.type.base_type == BASE_TYPE_UNION) {
        std::string field_table = GoIdentity(field.name, false) + "Table";
        code += "\t" + field_table + " := flatbuffers.Table{}\n";
        code +=
            "\tif rcv." + GoIdentity(field.name) + "(&" + field_table + ") {\n";
        code += "\t\tt." + field_name_camel + " = rcv." +
                GoIdentity(field.name + UnionTypeFieldSuffix()) + "().UnPack(" +
                field_table + ")\n";
        code += "\t}\n";
      } else {
        FLATBUFFERS_ASSERT(0);
      }
    }
    code += "}\n\n";

    // object api  UnPack
    code += "func (rcv *" + struct_def.name + ") UnPack() *" +
            NativeName(struct_def) + " {\n";
    code += "\tif rcv == nil {\n\t\treturn nil\n\t}\n";
    code += "\tt := &" + NativeName(struct_def) + "{}\n";
    code += "\trcv.UnPackTo(t)\n";
    code += "\treturn t\n";
    code += "}\n\n";
  }

  void GenNativeStructPack(const StructDef &struct_def, std::string *code_ptr) {
    std::string &code = *code_ptr;

    code += "func (t *" + NativeName(struct_def) +
            ") Pack(builder *flatbuffers.Builder) flatbuffers.UOffsetT {\n";
    code += "\tif t == nil {\n\t\treturn 0\n\t}\n";
    code += "\treturn Create" + struct_def.name + "(builder";
    StructPackArgs(struct_def, "", code_ptr);
    code += ")\n";
    code += "}\n\n";
  }

  void StructPackArgs(const StructDef &struct_def, const char *nameprefix,
                      std::string *code_ptr) {
    std::string &code = *code_ptr;
    for (auto it = struct_def.fields.vec.begin();
         it != struct_def.fields.vec.end(); ++it) {
      const FieldDef &field = **it;
      if (field.value.type.base_type == BASE_TYPE_STRUCT) {
        StructPackArgs(*field.value.type.struct_def,
                       (nameprefix + GoIdentity(field.name) + ".").c_str(),
                       code_ptr);
      } else {
        code += std::string(", t.") + nameprefix + GoIdentity(field.name);
      }
    }
  }

  void GenNativeStructUnPack(const StructDef &struct_def,
                             std::string *code_ptr) {
    std::string &code = *code_ptr;

    code += "func (rcv *" + struct_def.name + ") UnPackTo(t *" +
            NativeName(struct_def) + ") {\n";
    for (auto it = struct_def.fields.vec.begin();
         it != struct_def.fields.vec.end(); ++it) {
      const FieldDef &field = **it;
      if (field.value.type.base_type == BASE_TYPE_STRUCT) {
        code += "\tt." + GoIdentity(field.name) + " = rcv." +
                GoIdentity(field.name) + "(nil).UnPack()\n";
      } else {
        code += "\tt." + GoIdentity(field.name) + " = rcv." +
                GoIdentity(field.name) + "()\n";
      }
    }
    code += "}\n\n";

    code += "func (rcv *" + struct_def.name + ") UnPack() *" +
            NativeName(struct_def) + " {\n";
    code += "\tif rcv == nil {\n\t\treturn nil\n\t}\n";
    code += "\tt := &" + NativeName(struct_def) + "{}\n";
    code += "\trcv.UnPackTo(t)\n";
    code += "\treturn t\n";
    code += "}\n\n";
  }

  // Generate enum declarations.
  void GenEnum(const EnumDef &enum_def, std::string *code_ptr,
               bool *needs_imports) {
    if (enum_def.generated) return;

    auto max_name_length = MaxNameLength(enum_def);

    cur_name_space_ = enum_def.defined_namespace;

    // if namespace is missing, use root_type as namespace
    if (cur_name_space_->components.empty()) {
      if (parser_.root_struct_def_) {
        cur_name_space_->components.push_back(parser_.root_struct_def_->name);
      }
      // TODO: tsingson: if both namespace / root_type is missing, need
      // somewhere throw exception
    }

    GenComment(enum_def.doc_comment, code_ptr, nullptr);
    GenEnumType(enum_def, code_ptr);

    BeginEnum(code_ptr);
    for (auto it = enum_def.Vals().begin(); it != enum_def.Vals().end(); ++it) {
      const EnumVal &ev = **it;
      GenComment(ev.doc_comment, code_ptr, nullptr, "\t");
      EnumMember(enum_def, ev, max_name_length, code_ptr);
    }
    EndEnum(enum_def, code_ptr);

    BeginEnumNames(enum_def, code_ptr);
    for (auto it = enum_def.Vals().begin(); it != enum_def.Vals().end(); ++it) {
      const EnumVal &ev = **it;
      EnumNameMember(enum_def, ev, max_name_length, code_ptr);
    }
    EndEnumNames(code_ptr);

    BeginEnumValues(enum_def, code_ptr);
    for (auto it = enum_def.Vals().begin(); it != enum_def.Vals().end(); ++it) {
      auto &ev = **it;
      EnumValueMember(enum_def, ev, max_name_length, code_ptr);
    }
    EndEnumValues(code_ptr);

    EnumStringer(enum_def, code_ptr);

    // generate union's native object related go code
    if (enum_def.is_union && generate_object_based_api) {
      // union alone
      GenNativeUnion(enum_def, code_ptr);
      GenNativeUnionPack(enum_def, code_ptr);
      // union array ( unions vector )
      GenNativeUnionUnPack(enum_def, code_ptr);
      GenNativeUnionUnPackIn(enum_def, code_ptr);

      *needs_imports = true;
    }
  }

  // Returns the function name that is able to read a value of the given type.
  std::string GenGetter(const Type &type) {
    switch (type.base_type) {
      case BASE_TYPE_STRING: return "rcv._tab.ByteVector";
      case BASE_TYPE_UNION: return "rcv._tab.Union";
      case BASE_TYPE_VECTOR: return GenGetter(type.VectorType());
      default: return "rcv._tab.Get" + GoIdentity(GenTypeBasic(type));
    }
  }

  // Returns the method name for use with add/put calls.
  std::string GenMethod(const FieldDef &field) {
    return IsScalar(field.value.type.base_type)
               ? GoIdentity(GenTypeBasic(field.value.type))
               : (IsStruct(field.value.type) ? "Struct" : "UOffsetT");
  }

  std::string GenTypeBasic(const Type &type) {
    // clang-format off
    static const char *ctypename[] = {
#define FLATBUFFERS_TD(ENUM, IDLTYPE, CTYPE, JTYPE, GTYPE, ...) \
        #GTYPE,
        FLATBUFFERS_GEN_TYPES(FLATBUFFERS_TD)
#undef FLATBUFFERS_TD
    };
    // clang-format on
    return ctypename[type.base_type];
  }

  std::string GenTypePointer(const Type &type) {
    switch (type.base_type) {
      case BASE_TYPE_STRING: return "[]byte";
      case BASE_TYPE_VECTOR: return GenTypeGet(type.VectorType());
      case BASE_TYPE_STRUCT: return WrapInNameSpaceAndTrack(*type.struct_def);
      case BASE_TYPE_UNION:
        // fall through
      default: return "*flatbuffers.Table";
    }
  }

  std::string GenTypeGet(const Type &type) {
    if (type.enum_def != nullptr) { return GetEnumTypeName(*type.enum_def); }
    return IsScalar(type.base_type) ? GenTypeBasic(type) : GenTypePointer(type);
  }

  std::string TypeName(const FieldDef &field) {
    return GenTypeGet(field.value.type);
  }

  // If type is an enum, returns value with a cast to the enum type, otherwise
  // returns value as-is.
  std::string CastToEnum(const Type &type, std::string value) {
    if (type.enum_def == nullptr) {
      return value;
    } else {
      return GenTypeGet(type) + "(" + value + ")";
    }
  }

  // If type is an enum, returns value with a cast to the enum base type,
  // otherwise returns value as-is.
  std::string CastToBaseType(const Type &type, std::string value) {
    if (type.enum_def == nullptr) {
      return value;
    } else {
      return GenTypeBasic(type) + "(" + value + ")";
    }
  }

  std::string GenConstant(const FieldDef &field) {
    switch (field.value.type.base_type) {
      case BASE_TYPE_BOOL:
        return field.value.constant == "0" ? "false" : "true";
      default: return field.value.constant;
    }
  }

  std::string NativeName(const StructDef &struct_def) {
    return parser_.opts.object_prefix + struct_def.name +
           parser_.opts.object_suffix;
  }

  std::string NativeName(const EnumDef &enum_def) {
    return parser_.opts.object_prefix + enum_def.name +
           parser_.opts.object_suffix;
  }

  std::string NativeType(const Type &type) {
    // scalar field
    if (IsScalar(type.base_type)) {
      if (type.enum_def == nullptr) {
        // scalar field
        return GenTypeBasic(type);
      } else {
        // enum field
        return GetEnumTypeName(*type.enum_def);
      }
    }  // string vector field
    else if (type.base_type == BASE_TYPE_STRING) {
      return "string";
    }  // vector field
    else if (type.base_type == BASE_TYPE_VECTOR) {
      return "[]" + NativeType(type.VectorType());
    }  // struct field
    else if (type.base_type == BASE_TYPE_STRUCT) {
      return "*" + WrapInNameSpaceAndTrack(type.struct_def->defined_namespace,
                                           NativeName(*type.struct_def));
    }  // union field inside native object
    else if (type.base_type == BASE_TYPE_UNION) {
      return "*" + WrapInNameSpaceAndTrack(type.enum_def->defined_namespace,
                                           NativeName(*type.enum_def));
    }  // fixed length array field inside native object
    else if (type.base_type == BASE_TYPE_ARRAY) {
      return "[" + NumToString(type.fixed_length) + "]" +
             NativeType(type.VectorType());
    }
    FLATBUFFERS_ASSERT(0);
    return std::string();
  }

  std::string ReflectName(const StructDef &struct_def) {
    return parser_.opts.object_prefix + struct_def.name;
  }

  std::string ReflectName(const EnumDef &enum_def) {
    return parser_.opts.object_prefix + enum_def.name;
  }

  std::string ReflectType(const Type &type) {
    if (IsScalar(type.base_type)) {
      if (type.enum_def == nullptr) {
        return GenTypeBasic(type);
      } else {
        return GetEnumTypeName(*type.enum_def);
      }
    } else if (type.base_type == BASE_TYPE_STRING) {
      return "string";
    } else if (type.base_type == BASE_TYPE_VECTOR) {
      return ReflectType(type.VectorType());
    } else if (type.base_type == BASE_TYPE_STRUCT) {
      return ReflectName(*type.struct_def);
    } else if (type.base_type == BASE_TYPE_UNION) {
      return ReflectName(*type.enum_def);
    }
    FLATBUFFERS_ASSERT(0);
    return std::string();
  }

  // Create a struct with a builder and the struct's arguments.
  void GenStructBuilder(const StructDef &struct_def, std::string *code_ptr) {
    // struct builder declaration
    BeginBuilderArgs(struct_def, code_ptr);
    StructBuilderArgs(struct_def, "", code_ptr);
    EndBuilderArgs(code_ptr);
    // builder body, support fixed length array
    StructFixedBuilderBody(struct_def, "", code_ptr);
    EndBuilderBody(code_ptr);
  }

  // Begin by declaring namespace and imports.
  void BeginFile(const std::string &name_space_name, const bool needs_imports,
                 const bool is_enum, std::string *code_ptr) {
    std::string &code = *code_ptr;
    code = code +
           "// Code generated by the FlatBuffers compiler. DO NOT EDIT.\n\n";
    code += "package " + name_space_name + "\n\n";
    if (needs_imports) {
      code += "import (\n";
      if (is_enum) { code += "\t\"strconv\"\n\n"; }
      if (!parser_.opts.go_import.empty()) {
        code += "\tflatbuffers \"" + parser_.opts.go_import + "\"\n";
      } else {
        code += "\tflatbuffers \"github.com/google/flatbuffers/go\"\n";
      }
      if (tracked_imported_namespaces_.size() > 0) {
        for (auto it = tracked_imported_namespaces_.begin();
             it != tracked_imported_namespaces_.end(); ++it) {
          if (parser_.go_module_.length()) {
            code += "\t" + NamespaceImportName(*it) + " \"" +
                    parser_.go_module_ + NamespaceImportPath(*it) + "\"\n";
          } else {
            code += "\t" + NamespaceImportName(*it) + " \"" +
                    NamespaceImportPath(*it) + "\"\n";
          }
        }
      }
      code += ")\n\n";
    } else {
      if (is_enum) { code += "import \"strconv\"\n\n"; }
    }
  }

  // Save out the generated code for a Go Table type.
  bool SaveType(const Definition &def, const std::string *classcode,
                const bool needs_imports, const bool is_enum) {
    if (!classcode->length()) return true;

    Namespace &ns = go_namespace_.components.empty() ? *def.defined_namespace
                                                     : go_namespace_;

    std::string code = "";
    BeginFile(LastNamespacePart(ns), needs_imports, is_enum, &code);
    code += *classcode;
    // Strip extra newlines at end of file to make it gofmt-clean.
    while (code.length() > 2 && code.substr(code.length() - 2) == "\n\n") {
      code.pop_back();
    }
    std::string filename = NamespaceDir(ns) + def.name + ".go";
    return SaveFile(filename.c_str(), code, false);
  }

  // Create the full name of the imported namespace (format: A__B__C).
  std::string NamespaceImportName(const Namespace *ns) {
    std::string s = "";
    for (auto it = ns->components.begin(); it != ns->components.end(); ++it) {
      if (s.size() == 0) {
        s += *it;
      } else {
        s += "__" + *it;
      }
    }
    return s;
  }

  // Create the full path for the imported namespace (format: A/B/C).
  std::string NamespaceImportPath(const Namespace *ns) {
    std::string s = "";
    for (auto it = ns->components.begin(); it != ns->components.end(); ++it) {
      if (s.size() == 0) {
        s += *it;
      } else {
        s += "/" + *it;
      }
    }
    return s;
  }

  // Ensure that a type is prefixed with its go package import name if it is
  // used outside of its namespace.
  std::string WrapInNameSpaceAndTrack(const Namespace *ns,
                                      const std::string &name) {
    if (CurrentNameSpace() == ns) return name;

    tracked_imported_namespaces_.insert(ns);

    std::string import_name = NamespaceImportName(ns);
    return import_name + "." + name;
  }

  std::string WrapInNameSpacePrefix(const Namespace *ns) {
    if (CurrentNameSpace() == ns) return "";
    tracked_imported_namespaces_.insert(ns);
    std::string import_name = NamespaceImportName(ns);
    return import_name + ".";
  }

  std::string WrapInNameSpaceAndTrack(const Definition &def) {
    return WrapInNameSpaceAndTrack(def.defined_namespace, def.name);
  }

  const Namespace *CurrentNameSpace() const { return cur_name_space_; }

  static size_t MaxNameLength(const EnumDef &enum_def) {
    size_t max = 0;
    for (auto it = enum_def.Vals().begin(); it != enum_def.Vals().end(); ++it) {
      max = std::max((*it)->name.length(), max);
    }
    return max;
  }
};
}  // namespace go

bool GenerateGo(const Parser &parser, const std::string &path,
                const std::string &file_name) {
  go::GoGenerator generator(parser, path, file_name, parser.opts.go_namespace);
  return generator.generate();
}

}  // namespace flatbuffers
