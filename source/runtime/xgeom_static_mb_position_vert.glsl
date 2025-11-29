
#include "xgeom_static_mb_input_basics_vert.glsl"

//
// Vertex inputs
//
layout(location = 0) in ivec4 in_PosExtra;      // xyz = compressed pos (R16G16B16_SINT), w = extra 16 bits

//
// Gets the vertex local position
//
vec4 getVertexLocalPosition() 
{
    // Select cluster by push constant index
    ClusterData selectedCluster = cluster[push.clusterIndex];

    // Decode compressed position from int16 to [-1,1]
    const vec3 norm_pos = (vec3(in_PosExtra.xyz) + 32768.0) / 32767.5 - 1.0;
    return vec4(norm_pos * selectedCluster.posScale.xyz + selectedCluster.posTranslation.xyz, 1.0);
}