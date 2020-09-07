#ifndef MATERIALX_UNITY_SYNTAX_H
#define MATERIALX_UNITY_SYNTAX_H

/// @file
/// Unity syntax class

#include <MaterialXGenShader/Syntax.h>

namespace MaterialX
{

class UnitySyntax : public Syntax
{
  public:
    UnitySyntax();

    static SyntaxPtr create() { return std::make_shared<UnitySyntax>(); }

    const string& getInputQualifier() const override { return INPUT_QUALIFIER; }
    const string& getOutputQualifier() const override { return OUTPUT_QUALIFIER; }
    const string& getConstantQualifier() const override { return CONSTANT_QUALIFIER; }
    const string& getUniformQualifier() const override { return UNIFORM_QUALIFIER; }
    const string& getSourceFileExtension() const override { return SOURCE_FILE_EXTENSION; }

    bool typeSupported(const TypeDesc* type) const override;




    static const string INPUT_QUALIFIER;
    static const string OUTPUT_QUALIFIER;
    static const string UNIFORM_QUALIFIER;
    static const string CONSTANT_QUALIFIER;
    static const string SOURCE_FILE_EXTENSION;

    static const StringVec VEC2_MEMBERS;
    static const StringVec VEC3_MEMBERS;
    static const StringVec VEC4_MEMBERS;
};

}

#endif