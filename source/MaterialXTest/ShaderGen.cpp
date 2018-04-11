#include <MaterialXTest/Catch/catch.hpp>

#include <MaterialXCore/Document.h>
#include <MaterialXCore/Observer.h>

#include <MaterialXFormat/XmlIo.h>

#include <MaterialXShaderGen/ShaderGenerators/Glsl/GlslShaderGenerator.h>
#include <MaterialXShaderGen/ShaderGenerators/Glsl/GlslSyntax.h>
#include <MaterialXShaderGen/ShaderGenerators/Glsl/OgsFx/OgsFxShaderGenerator.h>
#include <MaterialXShaderGen/ShaderGenerators/Glsl/OgsFx/OgsFxSyntax.h>
#include <MaterialXShaderGen/ShaderGenerators/Osl/Arnold/ArnoldShaderGenerator.h>
#include <MaterialXShaderGen/ShaderGenerators/Osl/OslSyntax.h>
#include <MaterialXShaderGen/ShaderGenerators/Common/Swizzle.h>
#include <MaterialXShaderGen/Util.h>
#include <MaterialXShaderGen/HwShader.h>

#include <MaterialXView/Handlers/LightHandler.h>

#include <fstream>

namespace mx = MaterialX;

void loadLibraries(const mx::StringVec& libraryNames, const mx::FilePath& searchPath, mx::DocumentPtr doc)
{
    for (const std::string& library : libraryNames)
    {
        mx::FilePath path = searchPath / library;
        mx::StringVec filenames;
        mx::getDocumentsInDirectory(path, filenames);

        for (const std::string& filename : filenames)
        {
            mx::FilePath file = path / filename;
            mx::readFromXmlFile(doc, file);
        }
    }
}

void loadExamples(const mx::StringVec& exampleNames, const mx::FilePath& searchPath, mx::DocumentPtr doc)
{
    mx::StringVec filenames;
    mx::getDocumentsInDirectory(searchPath, filenames);
    for (const std::string& filename : exampleNames)
    {
        mx::FilePath file = searchPath / filename;
        mx::readFromXmlFile(doc, file);
    }
}


//
// Get source content, source path and resolved paths for
// an implementation
//
bool getShaderSource(mx::ShaderGeneratorPtr generator,
                    const mx::ImplementationPtr implementation,
                    mx::FilePath& sourcePath,
                    mx::FilePath& resolvedPath,
                    std::string& sourceContents) 
{
    if (implementation)
    {
        sourcePath = implementation->getFile();
        resolvedPath = generator->findSourceCode(sourcePath);
        if (mx::readFile(resolvedPath.asString(), sourceContents))
        {
            return true;
        }
    }
    return false;
}

// Check if a nodedef requires an implementation check
// Untyped nodes do not
bool requiresImplementation(const mx::NodeDefPtr nodeDef) 
{
    if (!nodeDef)
    {
        return false;
    }
    static std::string TYPE_NONE("none");
    const std::string typeAttribute = nodeDef->getAttribute(mx::Element::TYPE_ATTRIBUTE);
    return !typeAttribute.empty() && typeAttribute != TYPE_NONE;
}

void createLightCompoundExample(mx::DocumentPtr document)
{
    const std::string nodeName = "lightcompound";
    const std::string nodeDefName = "ND_" + nodeName;

    // Make sure it doesn't exists already
    if (!document->getNodeDef(nodeDefName))
    {
        // Create an interface for the light with position, color and intensity
        mx::NodeDefPtr nodeDef = document->addNodeDef(nodeDefName, "lightshader", nodeName);
        nodeDef->addInput("position", "vector3");
        nodeDef->addInput("color", "color3");
        nodeDef->addInput("intensity", "float");

        // Create a graph implementing the light using EDF's
        mx::NodeGraphPtr nodeGraph = document->addNodeGraph("IMP_" + nodeName);
        mx::OutputPtr output = nodeGraph->addOutput("out", "lightshader");

        // Add EDF node and connect the EDF's intensity to the 'color' input
        mx::NodePtr edf = nodeGraph->addNode("uniformedf", "edf1", "EDF");
        mx::InputPtr edf_intensity = edf->addInput("intensity", "color3");
        edf_intensity->setInterfaceName("color");

        // Add the light constructor node connect it's intensity to the 'intensity' input
        mx::NodePtr light = nodeGraph->addNode("light", "light1", "lightshader");
        mx::InputPtr light_intensity = light->addInput("intensity", "float");
        light_intensity->setInterfaceName("intensity");

        // Connect the EDF to the light constructor
        light->setConnectedNode("edf", edf);

        // Connect the light to the graph output
        output->setConnectedNode(light);

        // Make this graph become the implementation of our nodedef
        nodeGraph->setAttribute("nodedef", nodeDef->getName());
    }
}

float cosAngle(float degrees)
{
    static const float PI = 3.14159265f;
    return cos(degrees * PI / 180.0f);
}

void createLightRig(mx::DocumentPtr doc, mx::LightHandler& lightHandler, mx::HwShaderGenerator& shadergen)
{
    createLightCompoundExample(doc);

    mx::NodeDefPtr dirLightNodeDef = doc->getNodeDef("ND_directionallight");
    mx::NodeDefPtr pointLightNodeDef = doc->getNodeDef("ND_pointlight");
    mx::NodeDefPtr spotLightNodeDef = doc->getNodeDef("ND_spotlight");
    mx::NodeDefPtr compoundLightNodeDef = doc->getNodeDef("ND_lightcompound");
    REQUIRE(dirLightNodeDef != nullptr);
    REQUIRE(pointLightNodeDef != nullptr);
    REQUIRE(spotLightNodeDef != nullptr);
    REQUIRE(compoundLightNodeDef != nullptr);

    mx::LightSourcePtr dirLight = lightHandler.createLightSource(dirLightNodeDef);
    dirLight->setParameter("direction", mx::Vector3(0, 0, -1));
    dirLight->setParameter("color", mx::Color3(1, 1, 1));
    dirLight->setParameter("intensity", 0.2f);
    
    mx::LightSourcePtr pointLight = lightHandler.createLightSource(pointLightNodeDef);
    pointLight->setParameter("position", mx::Vector3(-2, -2, 2));
    pointLight->setParameter("color", mx::Color3(0, 0.0, 1));
    pointLight->setParameter("intensity", 10.0f);
    pointLight->setParameter("decayRate", 3.0f);

    // Make sure to send over normalized directions
    const float inv_dir_length = 1.0f / std::sqrt(3.0f);

    mx::LightSourcePtr spotLight = lightHandler.createLightSource(spotLightNodeDef);
    spotLight->setParameter("position", mx::Vector3(3, 3, 3));
    spotLight->setParameter("direction", mx::Vector3(-inv_dir_length, -inv_dir_length, -inv_dir_length));
    spotLight->setParameter("color", mx::Color3(1, 0, 0));
    spotLight->setParameter("intensity", 1.0f);
    spotLight->setParameter("decayRate", 0.0f);
    spotLight->setParameter("innerConeAngle", cosAngle(5.0f));
    spotLight->setParameter("outerConeAngle", cosAngle(10.0f));

    mx::LightSourcePtr compoundLight = lightHandler.createLightSource(compoundLightNodeDef);
    compoundLight->setParameter("position", mx::Vector3(-3, 3, 3));
    compoundLight->setParameter("direction", mx::Vector3(inv_dir_length, -inv_dir_length, -inv_dir_length));
    compoundLight->setParameter("color", mx::Color3(0, 1, 0));
    compoundLight->setParameter("intensity", 10.0f);

    lightHandler.bindLightShaders(shadergen);
}


TEST_CASE("OslSyntax", "[shadergen]")
{
    mx::SyntaxPtr syntax = mx::OslSyntax::creator();

    REQUIRE(syntax->getTypeName("float") == "float");
    REQUIRE(syntax->getTypeName("color3") == "color");
    REQUIRE(syntax->getTypeName("vector3") == "vector");

    REQUIRE(syntax->getTypeName("BSDF") == "BSDF");
    REQUIRE(syntax->getOutputTypeName("BSDF") == "output BSDF");

    std::string dv = syntax->getTypeDefault("float");
    REQUIRE(dv == "0.0");
    dv = syntax->getTypeDefault("color3", false);
    REQUIRE(dv == "color(0.0, 0.0, 0.0)");
    dv = syntax->getTypeDefault("color3", true);
    REQUIRE(dv == "color(0.0, 0.0, 0.0)");
}

TEST_CASE("GlslSyntax", "[shadergen]")
{
    mx::SyntaxPtr syntax = mx::GlslSyntax::creator();

    REQUIRE(syntax->getTypeName("float") == "float");
    REQUIRE(syntax->getTypeName("color3") == "vec3");
    REQUIRE(syntax->getTypeName("vector3") == "vec3");

    REQUIRE(syntax->getTypeName("BSDF") == "BSDF");
    REQUIRE(syntax->getOutputTypeName("BSDF") == "out BSDF");

    std::string dv = syntax->getTypeDefault("float");
    REQUIRE(dv == "0.0");
    dv = syntax->getTypeDefault("color3", false);
    REQUIRE(dv == "vec3(0.0)");
    dv = syntax->getTypeDefault("color3", true);
    REQUIRE(dv == "vec3(0.0)");
}

TEST_CASE("OgsFxSyntax", "[shadergen]")
{
    mx::SyntaxPtr syntax = mx::OgsFxSyntax::creator();

    REQUIRE(syntax->getTypeName("float") == "float");
    REQUIRE(syntax->getTypeName("color3") == "vec3");
    REQUIRE(syntax->getTypeName("vector3") == "vec3");

    REQUIRE(syntax->getTypeName("BSDF") == "BSDF");
    REQUIRE(syntax->getOutputTypeName("BSDF") == "out BSDF");

    std::string dv = syntax->getTypeDefault("float");
    REQUIRE(dv == "0.0");
    dv = syntax->getTypeDefault("color3", false);
    REQUIRE(dv == "vec3(0.0)");
    dv = syntax->getTypeDefault("color3", true);
    REQUIRE(dv == "{0.0, 0.0, 0.0}");
}

TEST_CASE("Swizzling", "[shadergen]")
{
    mx::DocumentPtr doc = mx::createDocument();

    mx::FilePath searchPath = mx::FilePath::getCurrentPath() / mx::FilePath("documents/Libraries");
    loadLibraries({"stdlib"}, searchPath, doc);

    mx::ArnoldShaderGenerator sg;
    sg.registerSourceCodeSearchPath(searchPath);
    mx::SyntaxPtr syntax = sg.getSyntax();

    // Test swizzle syntax
    std::string var1 = syntax->getSwizzledVariable("foo", "color3", "color3", "bgr");
    REQUIRE(var1 == "color(foo[2], foo[1], foo[0])");
    std::string var2 = syntax->getSwizzledVariable("foo", "color2", "vector2", "xy");
    REQUIRE(var2 == "color2(foo.x, foo.y)");
    std::string var3 = syntax->getSwizzledVariable("foo", "color4", "float", "rr01");
    REQUIRE(var3 == "color4_pack(foo, foo, 0, 1)");
    std::string var4 = syntax->getSwizzledVariable("foo", "vector3", "vector3", "zyx");
    REQUIRE(var4 == "vector(foo[2], foo[1], foo[0])");
    std::string var5 = syntax->getSwizzledVariable("foo", "vector2", "vector4", "yy");
    REQUIRE(var5 == "vector2(foo.y, foo.y)");
    std::string var6 = syntax->getSwizzledVariable("foo", "vector4", "color2", "rrgg");
    REQUIRE(var6 == "vector4(foo.r, foo.r, foo.a, foo.a)");

    // Create a simple test graph
    mx::NodeGraphPtr nodeGraph = doc->addNodeGraph();
    mx::NodePtr constant1 = nodeGraph->addNode("constant", "constant1", "color3");
    constant1->setParameterValue("value", mx::Color3(1, 2, 3));
    mx::NodePtr swizzle1 = nodeGraph->addNode("swizzle", "swizzle1", "color3");
    swizzle1->setConnectedNode("in", constant1);
    swizzle1->setParameterValue("channels", std::string("rrr"));
    mx::OutputPtr output1 = nodeGraph->addOutput();
    output1->setConnectedNode(swizzle1);

    // Test swizzle node implementation
    mx::Shader test1("test1");
    test1.initialize(output1, sg);
    mx::SgNode* sgNode = test1.getNodeGraph()->getNode("swizzle1");
    test1.addFunctionCall(sgNode, sg);
    const std::string test1Result =
        "color swizzle1_in = color(1, 2, 3);\n"
        "color swizzle1_out = color(swizzle1_in[0], swizzle1_in[0], swizzle1_in[0]);\n";
    REQUIRE(test1.getSourceCode() == test1Result);

    // Change swizzle pattern and test again
    swizzle1->setParameterValue("channels", std::string("b0b"));
    mx::Shader test2("test2");
    test2.initialize(output1, sg);
    sgNode = test2.getNodeGraph()->getNode("swizzle1");
    test2.addFunctionCall(sgNode, sg);
    const std::string test2Result =
        "color swizzle1_in = color(1, 2, 3);\n"
        "color swizzle1_out = color(swizzle1_in[2], 0, swizzle1_in[2]);\n";
    REQUIRE(test2.getSourceCode() == test2Result);
}

TEST_CASE("Hello World", "[shadergen]")
{
    mx::DocumentPtr doc = mx::createDocument();

    mx::FilePath searchPath = mx::FilePath::getCurrentPath() / mx::FilePath("documents/Libraries");
    loadLibraries({ "stdlib" }, searchPath, doc);

    const std::string exampleName = "hello_world";

    // Create the simples possible node graph,
    // containing a single constant node
    mx::NodeGraphPtr nodeGraph = doc->addNodeGraph("IMP_" + exampleName);
    mx::OutputPtr output1 = nodeGraph->addOutput();
    mx::NodePtr constant1 = nodeGraph->addNode("constant", "constant1", "color3");
    constant1->setParameterValue("value", mx::Color3(1, 1, 0));
    output1->setConnectedNode(constant1);

    // Create a nodedef and make its implementation be the graph above
    mx::NodeDefPtr nodeDef = doc->addNodeDef("ND_" + exampleName, "color3", exampleName);
    nodeGraph->setAttribute("nodedef", nodeDef->getName());

    // Create a material with the above node as the shader
    mx::MaterialPtr mtrl = doc->addMaterial(exampleName + "_material");
    mx::ShaderRefPtr shaderRef = mtrl->addShaderRef(exampleName + "_shader", exampleName);

    // Arnold OSL
    {
        mx::ShaderGeneratorPtr shadergen = mx::ArnoldShaderGenerator::creator();
        // Add path to find all source code snippets
        shadergen->registerSourceCodeSearchPath(searchPath);
        // Add path to find OSL include files
        shadergen->registerSourceCodeSearchPath(searchPath / mx::FilePath("stdlib/osl"));

        // Test shader generation from nodegraph
        mx::ShaderPtr shader = shadergen->generate(exampleName, output1);
        REQUIRE(shader != nullptr);
        REQUIRE(shader->getSourceCode().length() > 0);
        // Write out to file for inspection
        // TODO: Use validation in MaterialXView library
        std::ofstream file;
        file.open(shader->getName() + "_graph.osl");
        file << shader->getSourceCode();
        file.close();

        // Test shader generation from node
        shader = shadergen->generate(exampleName, constant1);
        REQUIRE(shader != nullptr);
        REQUIRE(shader->getSourceCode().length() > 0);
        // Write out to file for inspection
        // TODO: Use validation in MaterialXView library
        file.open(shader->getName() + "_node.osl");
        file << shader->getSourceCode();
        file.close();

        // Test shader generation from shaderref
        shader = shadergen->generate(exampleName, shaderRef);
        REQUIRE(shader != nullptr);
        REQUIRE(shader->getSourceCode().length() > 0);
        // Write out to file for inspection
        // TODO: Use validation in MaterialXView library
        file.open(shader->getName() + "_shaderref.osl");
        file << shader->getSourceCode();
        file.close();
    }

    // OgsFx
    {
        mx::ShaderGeneratorPtr shadergen = mx::OgsFxShaderGenerator::creator();
        shadergen->registerSourceCodeSearchPath(searchPath);

        // Test shader generation from nodegraph
        mx::ShaderPtr shader = shadergen->generate(exampleName, output1);
        REQUIRE(shader != nullptr);
        REQUIRE(shader->getSourceCode(mx::OgsFxShader::FINAL_FX_STAGE).length() > 0);
        // Write out to file for inspection
        // TODO: Use validation in MaterialXView library
        std::ofstream file;
        file.open(shader->getName() + "_graph.ogsfx");
        file << shader->getSourceCode(mx::OgsFxShader::FINAL_FX_STAGE);
        file.close();

        // Test shader generation from node
        shader = shadergen->generate(exampleName, constant1);
        REQUIRE(shader != nullptr);
        REQUIRE(shader->getSourceCode(mx::OgsFxShader::FINAL_FX_STAGE).length() > 0);
        // Write out to file for inspection
        // TODO: Use validation in MaterialXView library
        file.open(shader->getName() + "_node.ogsfx");
        file << shader->getSourceCode(mx::OgsFxShader::FINAL_FX_STAGE);
        file.close();

        // Test shader generation from shaderref
        shader = shadergen->generate(exampleName, shaderRef);
        REQUIRE(shader != nullptr);
        REQUIRE(shader->getSourceCode(mx::OgsFxShader::FINAL_FX_STAGE).length() > 0);
        // Write out to file for inspection
        // TODO: Use validation in MaterialXView library
        file.open(shader->getName() + "_shaderref.ogsfx");
        file << shader->getSourceCode(mx::OgsFxShader::FINAL_FX_STAGE);
        file.close();
    }

    // Glsl
    {
        mx::ShaderGeneratorPtr shadergen = mx::GlslShaderGenerator::creator();
        shadergen->registerSourceCodeSearchPath(searchPath);

        // Test shader generation from nodegraph
        mx::ShaderPtr shader = shadergen->generate(exampleName, output1);
        REQUIRE(shader != nullptr);
        REQUIRE(shader->getSourceCode(mx::OgsFxShader::VERTEX_STAGE).length() > 0);
        REQUIRE(shader->getSourceCode(mx::OgsFxShader::PIXEL_STAGE).length() > 0);
        // Write out to file for inspection
        // TODO: Use validation in MaterialXView library
        std::ofstream file;
        file.open(shader->getName() + "_graph.vert");
        file << shader->getSourceCode(mx::HwShader::VERTEX_STAGE);
        file.close();
        file.open(shader->getName() + "_graph.frag");
        file << shader->getSourceCode(mx::HwShader::PIXEL_STAGE);
        file.close();

        // Test shader generation from node
        shader = shadergen->generate(exampleName, constant1);
        REQUIRE(shader != nullptr);
        REQUIRE(shader->getSourceCode(mx::OgsFxShader::VERTEX_STAGE).length() > 0);
        REQUIRE(shader->getSourceCode(mx::OgsFxShader::PIXEL_STAGE).length() > 0);
        // Write out to file for inspection
        // TODO: Use validation in MaterialXView library
        file.open(shader->getName() + "_node.vert");
        file << shader->getSourceCode(mx::HwShader::VERTEX_STAGE);
        file.close();
        file.open(shader->getName() + "_node.frag");
        file << shader->getSourceCode(mx::HwShader::PIXEL_STAGE);
        file.close();

        // Test shader generation from node
        shader = shadergen->generate(exampleName, shaderRef);
        REQUIRE(shader != nullptr);
        REQUIRE(shader->getSourceCode(mx::OgsFxShader::VERTEX_STAGE).length() > 0);
        REQUIRE(shader->getSourceCode(mx::OgsFxShader::PIXEL_STAGE).length() > 0);
        // Write out to file for inspection
        // TODO: Use validation in MaterialXView library
        file.open(shader->getName() + "_shaderref.vert");
        file << shader->getSourceCode(mx::HwShader::VERTEX_STAGE);
        file.close();
        file.open(shader->getName() + "_shaderref.frag");
        file << shader->getSourceCode(mx::HwShader::PIXEL_STAGE);
        file.close();
    }
}

TEST_CASE("Conditionals", "[shadergen]")
{
    mx::DocumentPtr doc = mx::createDocument();

    mx::FilePath searchPath = mx::FilePath::getCurrentPath() / mx::FilePath("documents/Libraries");
    loadLibraries({ "stdlib" }, searchPath, doc);

    const std::string exampleName = "conditionals";

    // Create a simple node graph
    mx::NodeGraphPtr nodeGraph = doc->addNodeGraph(exampleName + "_graph");

    mx::NodePtr constant1 = nodeGraph->addNode("constant", "constant1", "color3");
    constant1->setParameterValue("value", mx::Color3(0, 0, 0));
    mx::NodePtr constant2 = nodeGraph->addNode("constant", "constant2", "color3");
    constant2->setParameterValue("value", mx::Color3(1, 1, 1));
    mx::NodePtr constant3 = nodeGraph->addNode("constant", "constant3", "float");
    constant3->setParameterValue("value", 0.5f);

    mx::NodePtr compare1 = nodeGraph->addNode("compare", "compare1", "color3");
    compare1->setConnectedNode("in1", constant1);
    compare1->setConnectedNode("in2", constant2);
    compare1->setConnectedNode("intest", constant3);

    mx::NodePtr constant4 = nodeGraph->addNode("constant", "constant4", "color3");
    constant4->setParameterValue("value", mx::Color3(1, 0, 0));
    mx::NodePtr constant5 = nodeGraph->addNode("constant", "constant5", "color3");
    constant5->setParameterValue("value", mx::Color3(0, 1, 0));
    mx::NodePtr constant6 = nodeGraph->addNode("constant", "constant6", "color3");
    constant6->setParameterValue("value", mx::Color3(0, 0, 1));

    mx::NodePtr switch1 = nodeGraph->addNode("switch", "switch1", "color3");
    switch1->setConnectedNode("in1", constant4);
    switch1->setConnectedNode("in2", constant5);
    switch1->setConnectedNode("in3", constant6);
    switch1->setConnectedNode("in4", compare1);
    switch1->setParameterValue<float>("which", 3);

    // Connected to output.
    mx::OutputPtr output1 = nodeGraph->addOutput();
    output1->setConnectedNode(switch1);

    // Write out a .dot file for visualization
    std::ofstream file;
    std::string dot = nodeGraph->asStringDot();
    file.open(nodeGraph->getName() + ".dot");
    file << dot;
    file.close();

    // Arnold
    {
        mx::ShaderGeneratorPtr shaderGenerator = mx::ArnoldShaderGenerator::creator();
        // Add path to find all source code snippets
        shaderGenerator->registerSourceCodeSearchPath(searchPath);
        // Add path to find OSL include files
        shaderGenerator->registerSourceCodeSearchPath(searchPath / mx::FilePath("stdlib/osl"));

        mx::ShaderPtr shader = shaderGenerator->generate(exampleName, output1);
        REQUIRE(shader != nullptr);
        REQUIRE(shader->getSourceCode().length() > 0);

        // All of the nodes should have been removed by optimization
        // leaving a graph with a single constant value
        REQUIRE(shader->getNodeGraph()->getNodes().empty());
        REQUIRE(shader->getNodeGraph()->getOutputSocket()->value->getValueString() == constant2->getParameterValue("value")->getValueString());

        // Write out to file for inspection
        // TODO: Use validation in MaterialXView library
        file.open(shader->getName() + ".osl");
        file << shader->getSourceCode();
        file.close();
    }

    // OgsFx
    {
        mx::ShaderGeneratorPtr shaderGenerator = mx::OgsFxShaderGenerator::creator();
        shaderGenerator->registerSourceCodeSearchPath(searchPath);

        mx::ShaderPtr shader = shaderGenerator->generate(exampleName, output1);
        REQUIRE(shader != nullptr);
        REQUIRE(shader->getSourceCode().length() > 0);

        // All of the nodes should have been removed by optimization
        // leaving a graph with a single constant value
        REQUIRE(shader->getNodeGraph()->getNodes().empty());
        REQUIRE(shader->getNodeGraph()->getOutputSocket()->value->getValueString() == constant2->getParameterValue("value")->getValueString());

        // Write out to file for inspection
        // TODO: Use validation in MaterialXView library
        file.open(shader->getName() + ".ogsfx");
        file << shader->getSourceCode();
        file.close();
    }

    // Glsl
    {
        mx::ShaderGeneratorPtr shaderGenerator = mx::GlslShaderGenerator::creator();
        shaderGenerator->registerSourceCodeSearchPath(searchPath);

        mx::ShaderPtr shader = shaderGenerator->generate(exampleName, output1);
        REQUIRE(shader != nullptr);
        REQUIRE(shader->getSourceCode(mx::HwShader::VERTEX_STAGE).length() > 0);
        REQUIRE(shader->getSourceCode(mx::HwShader::PIXEL_STAGE).length() > 0);

        // All of the nodes should have been removed by optimization
        // leaving a graph with a single constant value
        REQUIRE(shader->getNodeGraph()->getNodes().empty());
        REQUIRE(shader->getNodeGraph()->getOutputSocket()->value->getValueString() == constant2->getParameterValue("value")->getValueString());

        // Write out to file for inspection
        // TODO: Use validation in MaterialXView library
        file.open(shader->getName() + ".vert");
        file << shader->getSourceCode(mx::HwShader::VERTEX_STAGE);
        file.close();
        file.open(shader->getName() + ".frag");
        file << shader->getSourceCode(mx::HwShader::PIXEL_STAGE);
        file.close();
    }
}

TEST_CASE("Geometric Nodes", "[shadergen]")
{
    mx::DocumentPtr doc = mx::createDocument();

    mx::FilePath searchPath = mx::FilePath::getCurrentPath() / mx::FilePath("documents/Libraries");
    loadLibraries({ "stdlib" }, searchPath, doc);

    const std::string exampleName = "geometric_nodes";

    // Create a nonsensical graph testing some geometric nodes
    mx::NodeGraphPtr nodeGraph = doc->addNodeGraph("IMP_" + exampleName);

    mx::NodePtr normal1 = nodeGraph->addNode("normal", "normal1", "vector3");
    normal1->setParameterValue("space", std::string("world"));

    mx::NodePtr position1 = nodeGraph->addNode("position", "position1", "vector3");
    position1->setParameterValue("space", std::string("world"));

    mx::NodePtr texcoord1 = nodeGraph->addNode("texcoord", "texcoord1", "vector2");
    texcoord1->setParameterValue("index", 0, "integer");

    mx::NodePtr geomcolor1 = nodeGraph->addNode("geomcolor", "geomcolor1", "color3");
    geomcolor1->setParameterValue("index", 0, "integer");

    mx::NodePtr geomattrvalue1 = nodeGraph->addNode("geomattrvalue", "geomattrvalue1", "float");
    geomattrvalue1->setParameterValue("attrname", std::string("temperature"));

    mx::NodePtr add1 = nodeGraph->addNode("add", "add1", "vector3");
    add1->setConnectedNode("in1", normal1);
    add1->setConnectedNode("in2", position1);

    mx::NodePtr multiply1 = nodeGraph->addNode("multiply", "multiply1", "color3");
    multiply1->setConnectedNode("in1", geomcolor1);
    multiply1->setConnectedNode("in2", geomattrvalue1);

    mx::NodePtr convert1 = nodeGraph->addNode("swizzle", "convert1", "color3");
    convert1->setConnectedNode("in", add1);
    convert1->setParameterValue("channels", std::string("xyz"));

    mx::NodePtr multiply2 = nodeGraph->addNode("multiply", "multiply2", "color3");
    multiply2->setConnectedNode("in1", convert1);
    multiply2->setConnectedNode("in2", multiply1);

    mx::NodePtr time1 = nodeGraph->addNode("time", "time1", "float");
    mx::NodePtr multiply3 = nodeGraph->addNode("multiply", "multiply3", "color3");
    multiply3->setConnectedNode("in1", multiply2);
    multiply3->setConnectedNode("in2", time1);

    mx::NodePtr frame1 = nodeGraph->addNode("frame", "frame1", "float");
    mx::NodePtr multiply4 = nodeGraph->addNode("multiply", "multiply4", "color3");
    multiply4->setConnectedNode("in1", multiply3);
    multiply4->setConnectedNode("in2", frame1);

    // Connected to output.
    mx::OutputPtr output1 = nodeGraph->addOutput(mx::EMPTY_STRING, "color3");
    output1->setConnectedNode(multiply4);

    // Create a nodedef and make its implementation be the graph above
    mx::NodeDefPtr nodeDef = doc->addNodeDef("ND_" + exampleName, "color3", exampleName);
    nodeGraph->setAttribute("nodedef", nodeDef->getName());

    // Create a material with the above node as the shader
    mx::MaterialPtr mtrl = doc->addMaterial(exampleName + "_material");
    mx::ShaderRefPtr shaderRef = mtrl->addShaderRef(exampleName + "_shader", exampleName);

    // Arnold
    {
        mx::ShaderGeneratorPtr shaderGenerator = mx::ArnoldShaderGenerator::creator();
        // Add path to find all source code snippets
        shaderGenerator->registerSourceCodeSearchPath(searchPath);
        // Add path to find OSL include files
        shaderGenerator->registerSourceCodeSearchPath(searchPath / mx::FilePath("stdlib/osl"));

        mx::ShaderPtr shader = shaderGenerator->generate(exampleName, shaderRef);
        REQUIRE(shader != nullptr);
        REQUIRE(shader->getSourceCode().length() > 0);

        // Write out to file for inspection
        // TODO: Use validation in MaterialXView library
        std::ofstream file;
        file.open(shader->getName() + ".osl");
        file << shader->getSourceCode();
        file.close();
    }

    // OgsFx
    {
        mx::ShaderGeneratorPtr shaderGenerator = mx::OgsFxShaderGenerator::creator();
        shaderGenerator->registerSourceCodeSearchPath(searchPath);

        mx::ShaderPtr shader = shaderGenerator->generate(exampleName, shaderRef);
        REQUIRE(shader != nullptr);
        REQUIRE(shader->getSourceCode(mx::OgsFxShader::FINAL_FX_STAGE).length() > 0);

        // Write out to file for inspection
        // TODO: Use validation in MaterialXView library
        std::ofstream file;
        file.open(shader->getName() + ".ogsfx");
        file << shader->getSourceCode(mx::OgsFxShader::FINAL_FX_STAGE);
    }

    // Glsl
    {
        mx::ShaderGeneratorPtr shaderGenerator = mx::GlslShaderGenerator::creator();
        shaderGenerator->registerSourceCodeSearchPath(searchPath);

        mx::ShaderPtr shader = shaderGenerator->generate(exampleName, shaderRef);
        REQUIRE(shader != nullptr);
        REQUIRE(shader->getSourceCode(mx::HwShader::PIXEL_STAGE).length() > 0);
        REQUIRE(shader->getSourceCode(mx::HwShader::VERTEX_STAGE).length() > 0);

        // Write out to file for inspection
        // TODO: Use validation in MaterialXView library
        std::ofstream file;
        file.open(shader->getName() + ".frag");
        file << shader->getSourceCode(mx::HwShader::PIXEL_STAGE);
        file.close();
        file.open(shader->getName() + ".vert");
        file << shader->getSourceCode(mx::HwShader::VERTEX_STAGE);
    }
}

TEST_CASE("Noise", "[shadergen]")
{
    mx::DocumentPtr doc = mx::createDocument();

    mx::FilePath searchPath = mx::FilePath::getCurrentPath() / mx::FilePath("documents/Libraries");
    loadLibraries({ "stdlib" }, searchPath, doc);

    const std::string exampleName = "noise_test";

    // Create a node graph containing a noise node
    mx::NodeGraphPtr nodeGraph = doc->addNodeGraph("IMP_" + exampleName);
    mx::OutputPtr output1 = nodeGraph->addOutput("out", "color3");
    mx::NodePtr noise2d = nodeGraph->addNode("noise2d", "noise2d", "vector2");
    mx::NodePtr noise3d = nodeGraph->addNode("noise3d", "noise3d", "vector4");
    mx::NodePtr cellnoise2d = nodeGraph->addNode("cellnoise2d", "cellnoise2d", "float");
    mx::NodePtr cellnoise3d = nodeGraph->addNode("cellnoise3d", "cellnoise3d", "float");
    mx::NodePtr fractal3d = nodeGraph->addNode("fractal3d", "fractal3d", "float");
    noise2d->setParameterValue("amplitude", mx::Vector2(1.0,1.0));
    noise2d->setParameterValue("pivot", 0.0f);
    noise3d->setParameterValue("amplitude", 1.0);
    noise3d->setParameterValue("pivot", 0.0f);

    // Scale the noise2d uv's
    mx::NodePtr uv1 = nodeGraph->addNode("texcoord", "uv1", "vector2");
    mx::NodePtr uvmult1 = nodeGraph->addNode("multiply", "uvmult1", "vector2");
    uvmult1->setConnectedNode("in1", uv1);
    uvmult1->setInputValue("in2", mx::Vector2(16, 16));
    noise2d->setConnectedNode("texcoord", uvmult1);

    // Scale the noise3d position
    mx::NodePtr pos1 = nodeGraph->addNode("position", "pos1", "vector3");
    noise3d->setConnectedNode("position", pos1);
    mx::NodePtr posmult1 = nodeGraph->addNode("multiply", "posmult1", "vector3");
    posmult1->setConnectedNode("in1", pos1);
    posmult1->setInputValue("in2", mx::Vector3(16, 16, 16));
    noise3d->setConnectedNode("position", posmult1);

    cellnoise2d->setConnectedNode("texcoord", uvmult1);
    cellnoise3d->setConnectedNode("position", posmult1);
    fractal3d->setConnectedNode("position", posmult1);

    mx::NodePtr add1 = nodeGraph->addNode("add", "add1", "float");
    mx::NodePtr multiply1 = nodeGraph->addNode("multiply", "multiply1", "float");
    add1->setConnectedNode("in1", noise2d);
    add1->setInputValue("in2", 1.0f);
    multiply1->setConnectedNode("in1", add1);
    multiply1->setInputValue("in2", 0.5f);

    mx::NodePtr mixer = nodeGraph->addNode("mix", "mixer", "color3");
    mixer->setInputValue("fg", mx::Color3(1, 0, 0));
    mixer->setInputValue("bg", mx::Color3(1, 1, 0));
    mixer->setConnectedNode("mask", multiply1);

    output1->setConnectedNode(noise3d);
    output1->setChannels("www");

    // Arnold OSL
    {
        mx::ShaderGeneratorPtr shadergen = mx::ArnoldShaderGenerator::creator();
        // Add path to find all source code snippets
        shadergen->registerSourceCodeSearchPath(searchPath);
        // Add path to find OSL include files
        shadergen->registerSourceCodeSearchPath(searchPath / mx::FilePath("stdlib/osl"));

        // Test shader generation from nodegraph
        mx::ShaderPtr shader = shadergen->generate(exampleName, output1);
        REQUIRE(shader != nullptr);
        REQUIRE(shader->getSourceCode().length() > 0);
        // Write out to file for inspection
        // TODO: Use validation in MaterialXView library
        std::ofstream file;
        file.open(shader->getName() + ".osl");
        file << shader->getSourceCode();
        file.close();
    }

    // TODO: Implement the noise in GLSL
#if 1
    // OgsFx
    {
        mx::ShaderGeneratorPtr shadergen = mx::OgsFxShaderGenerator::creator();
        shadergen->registerSourceCodeSearchPath(searchPath);

        // Test shader generation from nodegraph
        mx::ShaderPtr shader = shadergen->generate(exampleName, output1);
        REQUIRE(shader != nullptr);
        REQUIRE(shader->getSourceCode(mx::OgsFxShader::FINAL_FX_STAGE).length() > 0);
        // Write out to file for inspection
        // TODO: Use validation in MaterialXView library
        std::ofstream file;
        file.open(shader->getName() + ".ogsfx");
        file << shader->getSourceCode(mx::OgsFxShader::FINAL_FX_STAGE);
        file.close();
    }

    // Glsl
    {
        mx::ShaderGeneratorPtr shadergen = mx::GlslShaderGenerator::creator();
        shadergen->registerSourceCodeSearchPath(searchPath);

        // Test shader generation from nodegraph
        mx::ShaderPtr shader = shadergen->generate(exampleName, output1);
        REQUIRE(shader != nullptr);
        REQUIRE(shader->getSourceCode(mx::OgsFxShader::VERTEX_STAGE).length() > 0);
        REQUIRE(shader->getSourceCode(mx::OgsFxShader::PIXEL_STAGE).length() > 0);
        // Write out to file for inspection
        // TODO: Use validation in MaterialXView library
        std::ofstream file;
        file.open(shader->getName() + ".vert");
        file << shader->getSourceCode(mx::HwShader::VERTEX_STAGE);
        file.close();
        file.open(shader->getName() + ".frag");
        file << shader->getSourceCode(mx::HwShader::PIXEL_STAGE);
        file.close();
    }
#endif
}

TEST_CASE("Subgraphs", "[shadergen]")
{
    mx::DocumentPtr doc = mx::createDocument();

    mx::FilePath searchPath = mx::FilePath::getCurrentPath() / mx::FilePath("documents/Libraries");
    loadLibraries({ "stdlib", "sxpbrlib" }, searchPath, doc);

    mx::FilePath examplesSearchPath = mx::FilePath::getCurrentPath() / mx::FilePath("documents/Examples");
    loadExamples({ "SubGraphs.mtlx"}, examplesSearchPath, doc);

    std::vector<std::string> exampleGraphNames = { "subgraph_ex1" , "subgraph_ex2" };

    // Arnold
    {
        mx::ShaderGeneratorPtr shaderGenerator = mx::ArnoldShaderGenerator::creator();
        // Add path to find all source code snippets
        shaderGenerator->registerSourceCodeSearchPath(searchPath);
        // Add path to find OSL include files
        shaderGenerator->registerSourceCodeSearchPath(searchPath / mx::FilePath("stdlib/osl"));

        for (const std::string& graphName : exampleGraphNames)
        {
            mx::NodeGraphPtr nodeGraph = doc->getNodeGraph(graphName);
            REQUIRE(nodeGraph != nullptr);

            mx::OutputPtr output = nodeGraph->getOutput("out");
            REQUIRE(output != nullptr);

            mx::ShaderPtr shader = shaderGenerator->generate(graphName, output);
            REQUIRE(shader != nullptr);
            REQUIRE(shader->getSourceCode().length() > 0);

            // Write out to file for inspection
            // TODO: Use validation in MaterialXView library
            std::ofstream file;
            file.open(shader->getName() + ".osl");
            file << shader->getSourceCode();
        }
    }

    // OgsFx
    {
        mx::ShaderGeneratorPtr shaderGenerator = mx::OgsFxShaderGenerator::creator();
        shaderGenerator->registerSourceCodeSearchPath(searchPath);

        // Setup lighting
        mx::LightHandlerPtr lightHandler = mx::LightHandler::creator();
        createLightRig(doc, *lightHandler, static_cast<mx::HwShaderGenerator&>(*shaderGenerator));

        for (const std::string& graphName : exampleGraphNames)
        {
            mx::NodeGraphPtr nodeGraph = doc->getNodeGraph(graphName);
            REQUIRE(nodeGraph != nullptr);

            mx::OutputPtr output = nodeGraph->getOutput("out");
            REQUIRE(output != nullptr);

            mx::ShaderPtr shader = shaderGenerator->generate(graphName, output);
            REQUIRE(shader != nullptr);
            REQUIRE(shader->getSourceCode(mx::OgsFxShader::FINAL_FX_STAGE).length() > 0);

            // Write out to file for inspection
            // TODO: Use validation in MaterialXView library
            std::ofstream file;
            file.open(shader->getName() + ".ogsfx");
            file << shader->getSourceCode(mx::OgsFxShader::FINAL_FX_STAGE);
        }
    }

    // Glsl
    {
        mx::ShaderGeneratorPtr shaderGenerator = mx::GlslShaderGenerator::creator();
        shaderGenerator->registerSourceCodeSearchPath(searchPath);

        // Setup lighting
        mx::LightHandlerPtr lightHandler = mx::LightHandler::creator();
        createLightRig(doc, *lightHandler, static_cast<mx::HwShaderGenerator&>(*shaderGenerator));

        for (const std::string& graphName : exampleGraphNames)
        {
            mx::NodeGraphPtr nodeGraph = doc->getNodeGraph(graphName);
            REQUIRE(nodeGraph != nullptr);

            mx::OutputPtr output = nodeGraph->getOutput("out");
            REQUIRE(output != nullptr);

            mx::ShaderPtr shader = shaderGenerator->generate(graphName, output);
            REQUIRE(shader != nullptr);

            REQUIRE(shader->getSourceCode(mx::HwShader::PIXEL_STAGE).length() > 0);
            REQUIRE(shader->getSourceCode(mx::HwShader::VERTEX_STAGE).length() > 0);

            // Write out to file for inspection
            // TODO: Use validation in MaterialXView library
            std::ofstream file;
            file.open(shader->getName() + ".frag");
            file << shader->getSourceCode(mx::HwShader::PIXEL_STAGE);
            file.close();
            file.open(shader->getName() + ".vert");
            file << shader->getSourceCode(mx::HwShader::VERTEX_STAGE);
        }
    }
}

TEST_CASE("BSDF Layering", "[shadergen]")
{
    mx::DocumentPtr doc = mx::createDocument();

    mx::FilePath searchPath = mx::FilePath::getCurrentPath() / mx::FilePath("documents/Libraries");
    loadLibraries({ "stdlib", "sxpbrlib" }, searchPath, doc);

    const std::string exampleName = "layered_bsdf";

    mx::NodeGraphPtr nodeGraph = doc->addNodeGraph("IMP_" + exampleName);

    // Diffuse component
    mx::NodePtr diffuse = nodeGraph->addNode("diffusebsdf", "diffuse", "BSDF");
    mx::InputPtr diffuse_color = diffuse->addInput("reflectance", "color3");
    diffuse_color->setPublicName("diffuse_color");
    diffuse_color->setValueString("0.9, 0.1, 0.1");

    // Translucent (thin walled SSS) component
    mx::NodePtr sss = nodeGraph->addNode("translucentbsdf", "sss", "BSDF");
    mx::InputPtr sss_color = sss->addInput("transmittance", "color3");
    sss_color->setPublicName("sss_color");
    sss_color->setValueString("0.1, 0.1, 0.8");

    // Mix diffuse over sss
    mx::NodePtr substrate = nodeGraph->addNode("mixbsdf", "substrate", "BSDF");
    mx::NodePtr substrate_weight_inv = nodeGraph->addNode("invert", "substrate_weight_inv", "float");
    substrate->setConnectedNode("in1", diffuse);
    substrate->setConnectedNode("in2", sss);
    substrate->setConnectedNode("weight", substrate_weight_inv);
    mx::InputPtr sss_weight = substrate_weight_inv->addInput("in", "float");
    sss_weight->setPublicName("sss_weight");
    sss_weight->setValueString("0.5");

    // Add a coating specular component on top
    mx::NodePtr coating = nodeGraph->addNode("coatingbsdf", "coating", "BSDF");
    coating->setConnectedNode("base", substrate);
    mx::InputPtr coating_color = coating->addInput("reflectance", "color3");
    coating_color->setPublicName("coating_color");
    coating_color->setValueString("1.0, 1.0, 1.0");
    mx::InputPtr coating_roughness = coating->addInput("roughness", "float");
    coating_roughness->setPublicName("coating_roughness");
    coating_roughness->setValueString("0.2");
    mx::InputPtr coating_ior = coating->addInput("ior", "float");
    coating_ior->setPublicName("coating_ior");
    coating_ior->setValueString("1.52");

    // Create a surface shader
    mx::NodePtr surface = nodeGraph->addNode("surface", "surface1", "surfaceshader");
    surface->setConnectedNode("bsdf", coating);

    // Connect to graph output
    mx::OutputPtr output = nodeGraph->addOutput("out", "surfaceshader");
    output->setConnectedNode(surface);

    // Create a nodedef and make its implementation be the graph above
    mx::NodeDefPtr nodeDef = doc->addNodeDef("ND_" + exampleName, "surfaceshader", exampleName);
    nodeGraph->setAttribute("nodedef", nodeDef->getName());

    // Create a material with the above node as the shader
    mx::MaterialPtr mtrl = doc->addMaterial(exampleName + "_material");
    mx::ShaderRefPtr shaderRef = mtrl->addShaderRef(exampleName + "_shader", exampleName);

    // Arnold
    {
        mx::ShaderGeneratorPtr shaderGenerator = mx::ArnoldShaderGenerator::creator();
        // Add path to find all source code snippets
        shaderGenerator->registerSourceCodeSearchPath(searchPath);
        // Add path to find OSL include files
        shaderGenerator->registerSourceCodeSearchPath(searchPath / mx::FilePath("stdlib/osl"));

        mx::ShaderPtr shader = shaderGenerator->generate(exampleName, shaderRef);
        REQUIRE(shader != nullptr);
        REQUIRE(shader->getSourceCode().length() > 0);

        // Write out to file for inspection
        // TODO: Use validation in MaterialXView library
        std::ofstream file;
        file.open(shader->getName() + ".osl");
        file << shader->getSourceCode();
        file.close();
    }

    // OgsFx
    {
        mx::ShaderGeneratorPtr shaderGenerator = mx::OgsFxShaderGenerator::creator();
        shaderGenerator->registerSourceCodeSearchPath(searchPath);

        // Setup lighting
        mx::LightHandlerPtr lightHandler = mx::LightHandler::creator();
        createLightRig(doc, *lightHandler, static_cast<mx::HwShaderGenerator&>(*shaderGenerator));

        mx::ShaderPtr shader = shaderGenerator->generate(exampleName, shaderRef);
        REQUIRE(shader != nullptr);
        REQUIRE(shader->getSourceCode(mx::OgsFxShader::FINAL_FX_STAGE).length() > 0);

        // Write out to file for inspection
        // TODO: Use validation in MaterialXView library
        std::ofstream file;
        file.open(shader->getName() + ".ogsfx");
        file << shader->getSourceCode(mx::OgsFxShader::FINAL_FX_STAGE);
    }

    // Glsl
    {
        mx::ShaderGeneratorPtr shaderGenerator = mx::GlslShaderGenerator::creator();
        shaderGenerator->registerSourceCodeSearchPath(searchPath);

        // Setup lighting
        mx::LightHandlerPtr lightHandler = mx::LightHandler::creator();
        createLightRig(doc, *lightHandler, static_cast<mx::HwShaderGenerator&>(*shaderGenerator));

        mx::ShaderPtr shader = shaderGenerator->generate(exampleName, shaderRef);
        REQUIRE(shader != nullptr);
        REQUIRE(shader->getSourceCode(mx::HwShader::PIXEL_STAGE).length() > 0);
        REQUIRE(shader->getSourceCode(mx::HwShader::VERTEX_STAGE).length() > 0);

        // Write out to file for inspection
        // TODO: Use validation in MaterialXView library
        std::ofstream file;
        file.open(shader->getName() + ".frag");
        file << shader->getSourceCode(mx::HwShader::PIXEL_STAGE);
        file.close();
        file.open(shader->getName() + ".vert");
        file << shader->getSourceCode(mx::HwShader::VERTEX_STAGE);
    }
}

TEST_CASE("Transparency", "[shadergen]")
{
    mx::DocumentPtr doc = mx::createDocument();

    mx::FilePath searchPath = mx::FilePath::getCurrentPath() / mx::FilePath("documents/Libraries");
    loadLibraries({ "stdlib", "sxpbrlib" }, searchPath, doc);

    const std::string exampleName = "transparent_surface";

    mx::NodeGraphPtr nodeGraph = doc->addNodeGraph("IMP_" + exampleName);

    mx::NodePtr refraction = nodeGraph->addNode("refractionbsdf", "refraction", "BSDF");
    mx::InputPtr transmittance = refraction->addInput("transmittance", "color3");
    transmittance->setPublicName("transmittance");
    transmittance->setValueString("0.0, 0.0, 0.0");

    mx::NodePtr coating = nodeGraph->addNode("coatingbsdf", "coating", "BSDF");
    coating->setConnectedNode("base", refraction);
    mx::InputPtr coating_color = coating->addInput("reflectance", "color3");
    coating_color->setPublicName("coating_color");
    coating_color->setValueString("1.0, 1.0, 1.0");

    mx::NodePtr ior_common = nodeGraph->addNode("constant", "ior_common", "float");
    mx::ParameterPtr ior = ior_common->addParameter("value", "float");
    ior->setPublicName("ior");
    ior->setValueString("1.52");
    coating->setConnectedNode("ior", ior_common);
    refraction->setConnectedNode("ior", ior_common);

    mx::NodePtr roughness_common = nodeGraph->addNode("constant", "roughness_common", "float");
    mx::ParameterPtr roughness = roughness_common->addParameter("value", "float");
    roughness->setPublicName("roughness");
    roughness->setValueString("0.2");
    coating->setConnectedNode("roughness", roughness_common);
    refraction->setConnectedNode("roughness", roughness_common);

    mx::NodePtr surface = nodeGraph->addNode("surface", "surface1", "surfaceshader");
    surface->setConnectedNode("bsdf", coating);
    mx::InputPtr opacity = surface->addInput("opacity", "float");
    opacity->setPublicName("opacity");
    opacity->setValueString("0.8");

    mx::OutputPtr output = nodeGraph->addOutput("out", "surfaceshader");
    output->setConnectedNode(surface);

    // Create a nodedef and make its implementation be the graph above
    mx::NodeDefPtr nodeDef = doc->addNodeDef("ND_" + exampleName, "surfaceshader", exampleName);
    nodeGraph->setAttribute("nodedef", nodeDef->getName());

    // Create a material with the above node as the shader
    mx::MaterialPtr mtrl = doc->addMaterial(exampleName + "_material");
    mx::ShaderRefPtr shaderRef = mtrl->addShaderRef(exampleName + "_shader", exampleName);

    // Arnold
    {
        mx::ShaderGeneratorPtr shaderGenerator = mx::ArnoldShaderGenerator::creator();
        // Add path to find all source code snippets
        shaderGenerator->registerSourceCodeSearchPath(searchPath);
        // Add path to find OSL include files
        shaderGenerator->registerSourceCodeSearchPath(searchPath / mx::FilePath("stdlib/osl"));

        mx::ShaderPtr shader = shaderGenerator->generate(exampleName, shaderRef);
        REQUIRE(shader != nullptr);
        REQUIRE(shader->getSourceCode().length() > 0);

        // Write out to file for inspection
        // TODO: Use validation in MaterialXView library
        std::ofstream file;
        file.open(shader->getName() + ".osl");
        file << shader->getSourceCode();
        file.close();
    }

    // OgsFx
    {
        mx::ShaderGeneratorPtr shaderGenerator = mx::OgsFxShaderGenerator::creator();
        shaderGenerator->registerSourceCodeSearchPath(searchPath);

        // Setup lighting
        mx::LightHandlerPtr lightHandler = mx::LightHandler::creator();
        createLightRig(doc, *lightHandler, static_cast<mx::HwShaderGenerator&>(*shaderGenerator));

        mx::ShaderPtr shader = shaderGenerator->generate(exampleName, shaderRef);
        REQUIRE(shader != nullptr);
        REQUIRE(shader->getSourceCode(mx::OgsFxShader::FINAL_FX_STAGE).length() > 0);

        // Write out to file for inspection
        // TODO: Use validation in MaterialXView library
        std::ofstream file;
        file.open(shader->getName() + ".ogsfx");
        file << shader->getSourceCode(mx::OgsFxShader::FINAL_FX_STAGE);
    }

    // Glsl
    {
        mx::ShaderGeneratorPtr shaderGenerator = mx::GlslShaderGenerator::creator();
        shaderGenerator->registerSourceCodeSearchPath(searchPath);

        // Setup lighting
        mx::LightHandlerPtr lightHandler = mx::LightHandler::creator();
        createLightRig(doc, *lightHandler, static_cast<mx::HwShaderGenerator&>(*shaderGenerator));

        mx::ShaderPtr shader = shaderGenerator->generate(exampleName, shaderRef);
        REQUIRE(shader != nullptr);
        REQUIRE(shader->getSourceCode(mx::HwShader::PIXEL_STAGE).length() > 0);
        REQUIRE(shader->getSourceCode(mx::HwShader::VERTEX_STAGE).length() > 0);

        // Write out to file for inspection
        // TODO: Use validation in MaterialXView library
        std::ofstream file;
        file.open(shader->getName() + ".frag");
        file << shader->getSourceCode(mx::HwShader::PIXEL_STAGE);
        file.close();
        file.open(shader->getName() + ".vert");
        file << shader->getSourceCode(mx::HwShader::VERTEX_STAGE);
    }
}

TEST_CASE("Surface Layering", "[shadergen]")
{
    mx::DocumentPtr doc = mx::createDocument();

    mx::FilePath searchPath = mx::FilePath::getCurrentPath() / mx::FilePath("documents/Libraries");
    loadLibraries({ "stdlib", "sxpbrlib" }, searchPath, doc);

    const std::string exampleName = "layered_surface";

    mx::NodeGraphPtr nodeGraph = doc->addNodeGraph("IMP_" + exampleName);

    // Create first surface layer from a surface with two BSDF's
    mx::NodePtr layer1_diffuse  = nodeGraph->addNode("diffusebsdf", "layer1_diffuse", "BSDF");
    mx::InputPtr layer1_diffuse_color = layer1_diffuse->addInput("reflectance", "color3");
    layer1_diffuse_color->setPublicName("layer1_diffuse");
    layer1_diffuse_color->setValueString("0.2, 0.9, 0.2");
    mx::NodePtr layer1_specular = nodeGraph->addNode("coatingbsdf", "layer1_specular", "BSDF");
    layer1_specular->setConnectedNode("base", layer1_diffuse);
    mx::InputPtr layer1_specular_color = layer1_specular->addInput("reflectance", "color3");
    layer1_specular_color->setPublicName("layer1_specular");
    layer1_specular_color->setValueString("1.0, 1.0, 1.0");
    mx::NodePtr layer1 = nodeGraph->addNode("surface", "layer1", "surfaceshader");
    layer1->setConnectedNode("bsdf", layer1_specular);

    // Create second surface layer from a standard uber shader
    mx::NodePtr layer2 = nodeGraph->addNode("standardsurface", "layer2", "surfaceshader");
    mx::InputPtr layer2_diffuse_color = layer2->addInput("base_color", "color3");
    layer2_diffuse_color->setPublicName("layer2_diffuse");
    layer2_diffuse_color->setValueString("0.9, 0.1, 0.2");
    mx::InputPtr layer2_specular_color = layer2->addInput("specular_color", "color3");
    layer2_specular_color->setPublicName("layer2_specular");
    layer2_specular_color->setValueString("1.0, 1.0, 1.0");

    // Create layer mixer
    mx::NodePtr mixer = nodeGraph->addNode("layeredsurface", "mixer", "surfaceshader");
    mixer->setConnectedNode("top", layer2);
    mixer->setConnectedNode("base", layer1);
    mx::InputPtr mix_weight = mixer->addInput("weight", "float");
    mix_weight->setPublicName("mix_weight");
    mix_weight->setValueString("1.0");

    // Connect to graph output
    mx::OutputPtr output = nodeGraph->addOutput("out", "surfaceshader");
    output->setConnectedNode(mixer);

    // Create a nodedef and make its implementation be the graph above
    mx::NodeDefPtr nodeDef = doc->addNodeDef("ND_" + exampleName, "surfaceshader", exampleName);
    nodeGraph->setAttribute("nodedef", nodeDef->getName());

    // Create a material with the above node as the shader
    mx::MaterialPtr mtrl = doc->addMaterial(exampleName + "_material");
    mx::ShaderRefPtr shaderRef = mtrl->addShaderRef(exampleName + "_shader", exampleName);

    // OgsFx
    {
        mx::ShaderGeneratorPtr shaderGenerator = mx::OgsFxShaderGenerator::creator();
        shaderGenerator->registerSourceCodeSearchPath(searchPath);

        // Setup lighting
        mx::LightHandlerPtr lightHandler = mx::LightHandler::creator();
        createLightRig(doc, *lightHandler, static_cast<mx::HwShaderGenerator&>(*shaderGenerator));

        mx::ShaderPtr shader = shaderGenerator->generate(exampleName, shaderRef);
        REQUIRE(shader != nullptr);
        REQUIRE(shader->getSourceCode(mx::OgsFxShader::FINAL_FX_STAGE).length() > 0);

        // Write out to file for inspection
        // TODO: Use validation in MaterialXView library
        std::ofstream file;
        file.open(shader->getName() + ".ogsfx");
        file << shader->getSourceCode(mx::OgsFxShader::FINAL_FX_STAGE);
    }

    // Glsl
    {
        mx::ShaderGeneratorPtr shaderGenerator = mx::GlslShaderGenerator::creator();
        shaderGenerator->registerSourceCodeSearchPath(searchPath);

        // Setup lighting
        mx::LightHandlerPtr lightHandler = mx::LightHandler::creator();
        createLightRig(doc, *lightHandler, static_cast<mx::HwShaderGenerator&>(*shaderGenerator));

        mx::ShaderPtr shader = shaderGenerator->generate(exampleName, shaderRef);
        REQUIRE(shader != nullptr);
        REQUIRE(shader->getSourceCode(mx::HwShader::PIXEL_STAGE).length() > 0);
        REQUIRE(shader->getSourceCode(mx::HwShader::VERTEX_STAGE).length() > 0);

        // Write out to file for inspection
        // TODO: Use validation in MaterialXView library
        std::ofstream file;
        file.open(shader->getName() + ".frag");
        file << shader->getSourceCode(mx::HwShader::PIXEL_STAGE);
        file.close();
        file.open(shader->getName() + ".vert");
        file << shader->getSourceCode(mx::HwShader::VERTEX_STAGE);
    }
}

TEST_CASE("Osl Output Types", "[shadergen]")
{
    // OSL doesn't support having color2/color4 as shader output types.
    // The color2/color4 types are custom struct types added by MaterialX.
    // It's actually crashing the OSL compiler right now.
    // TODO: Report this problem to the OSL team.
    //
    // This test makes sure that color2/color4/vector2/vector4 gets converted
    // to color/vector when used as shader outputs.

    mx::DocumentPtr doc = mx::createDocument();

    mx::FilePath searchPath = mx::FilePath::getCurrentPath() / mx::FilePath("documents/Libraries");
    loadLibraries({ "stdlib" }, searchPath, doc);

    const std::string exampleName = "osl_output";

    mx::NodeGraphPtr nodeGraph1 = doc->addNodeGraph();
    mx::OutputPtr output1 = nodeGraph1->addOutput(mx::EMPTY_STRING, "color2");
    mx::NodePtr node1 = nodeGraph1->addNode("remap", mx::EMPTY_STRING, "color2");
    output1->setConnectedNode(node1);
    mx::NodeDefPtr nodeDef1 = doc->addNodeDef(mx::EMPTY_STRING, "color2", exampleName + "_color2");
    nodeGraph1->setAttribute("nodedef", nodeDef1->getName());

    mx::NodeGraphPtr nodeGraph2 = doc->addNodeGraph();
    mx::OutputPtr output2 = nodeGraph2->addOutput(mx::EMPTY_STRING, "color4");
    mx::NodePtr node2 = nodeGraph2->addNode("remap", mx::EMPTY_STRING, "color4");
    output2->setConnectedNode(node2);
    mx::NodeDefPtr nodeDef2 = doc->addNodeDef(mx::EMPTY_STRING, "color4", exampleName + "_color4");
    nodeGraph2->setAttribute("nodedef", nodeDef2->getName());

    {
        mx::ShaderGeneratorPtr shadergen = mx::ArnoldShaderGenerator::creator();
        // Add path to find all source code snippets
        shadergen->registerSourceCodeSearchPath(searchPath);
        // Add path to find OSL include files
        shadergen->registerSourceCodeSearchPath(searchPath / mx::FilePath("stdlib/osl"));

        // Test shader generation from color2 type graph
        mx::ShaderPtr shader = shadergen->generate(exampleName + "_color2", output1);
        REQUIRE(shader != nullptr);
        REQUIRE(shader->getSourceCode().length() > 0);
        // Write out to file for inspection
        // TODO: Use validation in MaterialXView library
        std::ofstream file;
        file.open(exampleName + "_color2.osl");
        file << shader->getSourceCode();
        file.close();

        // Test shader generation from color4 type graph
        shader = shadergen->generate(exampleName + "_color4", output2);
        REQUIRE(shader != nullptr);
        REQUIRE(shader->getSourceCode().length() > 0);
        // Write out to file for inspection
        // TODO: Use validation in MaterialXView library
        file.open(exampleName + "_color4.osl");
        file << shader->getSourceCode();
        file.close();
    }

    // Change to vector2/vector4 types
    output1->setType("vector2");
    node1->setType("vector2");
    nodeDef1->setType("vector2");
    output2->setType("vector4");
    node2->setType("vector4");
    nodeDef2->setType("vector4");

    // Add swizzling to make sure type remapping works with swizzling
    output1->setChannels("yx");
    output2->setChannels("wzyx");

    {
        mx::ShaderGeneratorPtr shadergen = mx::ArnoldShaderGenerator::creator();
        // Add path to find all source code snippets
        shadergen->registerSourceCodeSearchPath(searchPath);
        // Add path to find OSL include files
        shadergen->registerSourceCodeSearchPath(searchPath / mx::FilePath("stdlib/osl"));

        // Test shader generation from color2 type graph
        mx::ShaderPtr shader = shadergen->generate(exampleName + "_vector2", output1);
        REQUIRE(shader != nullptr);
        REQUIRE(shader->getSourceCode().length() > 0);
        // Write out to file for inspection
        // TODO: Use validation in MaterialXView library
        std::ofstream file;
        file.open(exampleName + "_vector2.osl");
        file << shader->getSourceCode();
        file.close();

        // Test shader generation from color4 type graph
        shader = shadergen->generate(exampleName + "_vector4", output2);
        REQUIRE(shader != nullptr);
        REQUIRE(shader->getSourceCode().length() > 0);
        // Write out to file for inspection
        // TODO: Use validation in MaterialXView library
        file.open(exampleName + "_vector4.osl");
        file << shader->getSourceCode();
        file.close();
    }
}

TEST_CASE("Reference Implementation Validity", "[shadergen]")
{
    std::filebuf implDumpBuffer;
    std::string fileName = "reference_implementation_check.txt";
    implDumpBuffer.open(fileName, std::ios::out);
    std::ostream implDumpStream(&implDumpBuffer);

    implDumpStream << "-----------------------------------------------------------------------" << std::endl;
    implDumpStream << "Scanning language: osl. Target: reference" << std::endl;
    implDumpStream << "-----------------------------------------------------------------------" << std::endl;

    mx::DocumentPtr doc = mx::createDocument();

    mx::FilePath searchPath = mx::FilePath::getCurrentPath() / mx::FilePath("documents/Libraries");
    loadLibraries({ "stdlib" }, searchPath, doc);

    // Set source code search path
    mx::FileSearchPath sourceCodeSearchPath;
    sourceCodeSearchPath.append(searchPath);

    std::string nodeDefNode;
    std::string nodeDefType;
    unsigned int count = 0;
    unsigned int missing = 0;
    std::string missing_str;
    std::string found_str;

    // Scan through every nodedef defined
    for (mx::NodeDefPtr nodeDef : doc->getNodeDefs())
    {
        count++;

        const std::string language("osl");
        const std::string target("");

        std::string nodeDefName = nodeDef->getName();
        std::string nodeName = nodeDef->getNodeString();
        if (!requiresImplementation(nodeDef))
        {
            found_str += "No implementation required for nodedef: " + nodeDefName + ", Node: " + nodeName + ".\n";
            continue;
        }

        mx::InterfaceElementPtr inter = nodeDef->getImplementation(language, target);
        if (!inter)
        {
            missing++;
            missing_str += "Missing nodeDef implemenation: " + nodeDefName + ", Node: " + nodeName + ".\n";
        }
        else
        {
            mx::ImplementationPtr impl = inter->asA<mx::Implementation>();
            if (impl)
            {
                // Scan for file and see if we can read in the contents
                std::string sourceContents;
                mx::FilePath sourcePath = impl->getFile();
                mx::FilePath resolvedPath = sourceCodeSearchPath.find(sourcePath);
                bool found = mx::readFile(resolvedPath.asString(), sourceContents);
                if (!found)
                {
                    missing++;
                    missing_str += "Missing source code: " + sourcePath.asString() + " for nodeDef: "
                        + nodeDefName + ". Impl: " + impl->getName() + ".\n";
                }
                else
                {
                    found_str += "Found impl and src for nodedef: " + nodeDefName + ", Node: "
                        + nodeName + ". Impl: " + impl->getName() + ".\n";
                }
            }
            else
            {
                mx::NodeGraphPtr graph = inter->asA<mx::NodeGraph>();
                found_str += "Found NodeGraph impl for nodedef: " + nodeDefName + ", Node: "
                    + nodeName + ". Impl: " + graph->getName() + ".\n";
            }
        }
    }

    implDumpStream << "-----------------------------------------------------------------------" << std::endl;
    implDumpStream << "Missing: " << missing << " implementations out of: " << count << " nodedefs\n";
    implDumpStream << missing_str << std::endl;
    implDumpStream << found_str << std::endl;
    implDumpStream << "-----------------------------------------------------------------------" << std::endl;

    implDumpBuffer.close();

    // To enable once this is true
    //REQUIRE(missing == 0);
}

TEST_CASE("ShaderX Implementation Validity", "[shadergen]")
{
    mx::DocumentPtr doc = mx::createDocument();

    mx::FilePath searchPath = mx::FilePath::getCurrentPath() / mx::FilePath("documents/Libraries");
    loadLibraries({ "stdlib" "sxpbrlib" }, searchPath, doc);

    std::vector<mx::ShaderGeneratorPtr> shaderGenerators =
    {
        mx::ArnoldShaderGenerator::creator(),
        mx::OgsFxShaderGenerator::creator(),
        mx::GlslShaderGenerator::creator()
    };

    std::filebuf implDumpBuffer;
    std::string fileName = "shadgen_implementation_check.txt";
    implDumpBuffer.open(fileName, std::ios::out);
    std::ostream implDumpStream(&implDumpBuffer);

    for (auto generator : shaderGenerators)
    {
        generator->registerSourceCodeSearchPath(searchPath);

        const std::string& language = generator->getLanguage();
        const std::string& target = generator->getTarget();

        implDumpStream << "-----------------------------------------------------------------------" << std::endl;
        implDumpStream << "Scanning language: " << language << ". Target: " << target << std::endl;
        implDumpStream << "-----------------------------------------------------------------------" << std::endl;

        std::string nodeDefNode;
        std::string nodeDefType;
        unsigned int count = 0;
        unsigned int missing = 0;
        std::string missing_str;
        std::string found_str;

        // Scan through every nodedef defined
        for (mx::NodeDefPtr nodeDef : doc->getNodeDefs())
        {
            count++;

            std::string nodeDefName = nodeDef->getName();
            std::string nodeName = nodeDef->getNodeString();
            if (!requiresImplementation(nodeDef))
            {
                found_str += "No implementation requried for nodedef: " + nodeDefName + ", Node: " + nodeName + ".\n";
                continue;
            }

            mx::InterfaceElementPtr inter = nodeDef->getImplementation(language, target);
            if (!inter)
            {
                missing++;
                missing_str += "Missing nodeDef implementation: " + nodeDefName + ", Node: " + nodeName + ".\n";
            }
            else
            {
                mx::ImplementationPtr impl = inter->asA<mx::Implementation>();
                if (impl)
                {
                    // Test if the generator has an interal implementation first
                    if (generator->implementationRegistered(impl->getName()))
                    {
                        found_str += "Found generator impl for nodedef: " + nodeDefName + ", Node: "
                            + nodeDefName + ". Impl: " + impl->getName() + ".\n";
                    }

                    // Check for an implementation explicitly stored
                    else
                    {
                        mx::FilePath sourcePath, resolvedPath;
                        std::string contents;
                        if (!getShaderSource(generator, impl, sourcePath, resolvedPath, contents))
                        {
                            missing++;
                            missing_str += "Missing source code: " + sourcePath.asString() + " for nodeDef: "
                                + nodeDefName + ". Impl: " + impl->getName() + ".\n";
                        }
                        else
                        {
                            found_str += "Found impl and src for nodedef: " + nodeDefName + ", Node: "
                                + nodeName + +". Impl: " + impl->getName() + ".\n";
                        }
                    }
                }
                else
                {
                    mx::NodeGraphPtr graph = inter->asA<mx::NodeGraph>();
                    found_str += "Found NodeGraph impl for nodedef: " + nodeDefName + ", Node: "
                        + nodeName + ". Impl: " + graph->getName() + ".\n";
                }
            }
        }

        implDumpStream << "-----------------------------------------------------------------------" << std::endl;
        implDumpStream << "Missing: " << missing << " implementations out of: " << count << " nodedefs\n";
        implDumpStream << missing_str << std::endl;
        implDumpStream << found_str << std::endl;
        implDumpStream << "-----------------------------------------------------------------------" << std::endl;

        // To enable once this is true
        //REQUIRE(missing == 0);
    }

    implDumpBuffer.close();
}
