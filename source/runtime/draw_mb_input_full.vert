#include "mb_input_definition_full.vert"

layout(location = 0) in vec3 inPos;     //[INPUT_POSITION]
layout(location = 1) in vec3 inNormal;  //[INPUT_NORMAL]
layout(location = 2) in vec3 inTangent; //[INPUT_TANGENT]
layout(location = 3) in vec2 inUV;      //[INPUT_UVS]
layout(location = 4) in vec4 inColor;   //[INPUT_COLOR]


mb_full_vertex getVertex()
{
    mb_full_vertex Data;
    Data.LocalPos   = inPos;
    Data.VertColor  = inColor;
    Data.Tangent    = inTangent;
    Data.Binormal   = cross(inTangent, inNormal);
    Data.Normal     = inNormal;
    Data.UV         = inUV;

    return Data;
}