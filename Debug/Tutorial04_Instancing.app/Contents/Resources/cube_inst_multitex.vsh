cbuffer Constants
{
    float4x4 g_ViewProj;
    float4x4 g_Rotation;
};

struct VSInput
{
    // Vertex attributes
    float3 Pos      : ATTRIB0;
    float2 UV       : ATTRIB1;

    // Instance attributes
    float4 MtrxRow0 : ATTRIB2;
    float4 MtrxRow1 : ATTRIB3;
    float4 MtrxRow2 : ATTRIB4;
    float4 MtrxRow3 : ATTRIB5;
    float  TexSelector : ATTRIB6;
};

struct PSInput
{
    float4 Pos          : SV_POSITION;
    float2 UV           : TEX_COORD;
    float  TexSelector  : TEXCOORD1;
};

void main(in VSInput VSIn, out PSInput PSIn)
{
    // HLSL matrices are row-major while GLSL matrices are column-major. We will
    // use convenience function MatrixFromRows() appropriately defined by the engine
    float4x4 InstanceMatr = MatrixFromRows(VSIn.MtrxRow0, VSIn.MtrxRow1, VSIn.MtrxRow2, VSIn.MtrxRow3);
    
    // Apply rotation
    float4 TransformedPos = mul(float4(VSIn.Pos,1.0), g_Rotation);
    
    // Apply instance-specific transformation
    TransformedPos = mul(TransformedPos, InstanceMatr);
    
    // Apply view-projection matrix
    PSIn.Pos = mul(TransformedPos, g_ViewProj);
    
    // Pasar coordenadas UV y selector de textura al pixel shader
    PSIn.UV = VSIn.UV;
    PSIn.TexSelector = VSIn.TexSelector;
}
