#include <MaterialXGenUnity/UnitySyntax.h>

#include <MaterialXGenShader/Library.h>
#include <MaterialXGenShader/TypeDesc.h>

namespace MaterialX
{

namespace
{



}

const string UnitySyntax::INPUT_QUALIFIER = "in";
const string UnitySyntax::OUTPUT_QUALIFIER = "out";
const string UnitySyntax::UNIFORM_QUALIFIER = "uniform";
const string UnitySyntax::CONSTANT_QUALIFIER = "const";
const string UnitySyntax::SOURCE_FILE_EXTENSION = ".shader";

const StringVec UnitySyntax::VEC2_MEMBERS = { ".x", ".y" };
const StringVec UnitySyntax::VEC3_MEMBERS = { ".x", ".y", ".z "};
const StringVec UnitySyntax::VEC4_MEMBERS = { ".x", ".y", ".z", ".w" };

}