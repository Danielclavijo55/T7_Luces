cbuffer Constants
{
    float4x4 g_LightViewProj;
    float4x4 g_Rotation;
};

struct VSInput
{
    float3 Pos      : ATTRIB0;
    float2 UV       : ATTRIB1;
    float4 MtrxRow0 : ATTRIB2;
    float4 MtrxRow1 : ATTRIB3;
    float4 MtrxRow2 : ATTRIB4;
    float4 MtrxRow3 : ATTRIB5;
    float  TexSelector : ATTRIB6;
};

struct PSInput
{
    float4 Pos : SV_POSITION;
};

void main(in VSInput VSIn, out PSInput PSIn)
{
    PSIn.Pos = float4(VSIn.Pos, 1.0);
}
