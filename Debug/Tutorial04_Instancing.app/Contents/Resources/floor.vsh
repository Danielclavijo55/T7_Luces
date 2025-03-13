cbuffer Constants
{
    float4x4 g_Model;     // Matriz de modelo
    float4x4 g_ViewProj;  // Matriz de vista-proyección
};

struct VSInput
{
    float3 Pos : ATTRIB0;  // Posición
    float2 UV  : ATTRIB1;  // Coordenadas de textura
};

struct PSInput
{
    float4 Pos     : SV_POSITION;
    float2 UV      : TEX_COORD;
    float3 Normal  : NORMAL;
};

void main(in VSInput VSIn, out PSInput PSIn)
{
    // Transformar posición a espacio de mundo
    float4 worldPos = mul(float4(VSIn.Pos, 1.0), g_Model);
    
    // Aplicar matriz vista-proyección
    PSIn.Pos = mul(worldPos, g_ViewProj);
    
    // Pasar coordenadas UV sin modificar
    PSIn.UV = VSIn.UV;
    
    // El suelo es un plano XZ, así que la normal siempre apunta hacia arriba (Y)
    PSIn.Normal = float3(0.0, 1.0, 0.0);
}
