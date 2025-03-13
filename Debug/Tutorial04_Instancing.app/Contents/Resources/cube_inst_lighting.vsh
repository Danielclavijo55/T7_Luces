cbuffer Constants
{
    float4x4 g_ViewProj;     // Matriz de vista-proyección
    float4x4 g_Rotation;     // Matriz de rotación global
    float4   g_LightDir;     // Dirección de la luz
    float4   g_CameraPos;    // Posición de la cámara
};

struct VSInput
{
    float3 Pos      : ATTRIB0;  // Posición del vértice
    float2 UV       : ATTRIB1;  // Coordenada de textura
    
    // Datos de instancia
    float4 MtrxRow0 : ATTRIB2;  // Primera fila de la matriz de instancia
    float4 MtrxRow1 : ATTRIB3;  // Segunda fila de la matriz de instancia
    float4 MtrxRow2 : ATTRIB4;  // Tercera fila de la matriz de instancia
    float4 MtrxRow3 : ATTRIB5;  // Cuarta fila de la matriz de instancia
    float  TexSelector : ATTRIB6; // Selector de textura
};

struct PSInput
{
    float4 Pos          : SV_POSITION;  // Posición en espacio de pantalla
    float2 UV           : TEX_COORD;    // Coordenada de textura
    float  TexSelector  : TEXCOORD1;    // Selector de textura
    float3 Normal       : NORMAL;       // Normal en espacio de mundo
    float3 WorldPos     : TEXCOORD2;    // Posición en espacio de mundo
};

// Función para determinar la normal basada en la posición del vértice
float3 CalculateNormal(float3 pos)
{
    // Para un cubo, la normal es la dirección desde el centro hacia el vértice
    // pero normalizada y alineada con los ejes principales
    float3 normal = float3(0.0, 0.0, 0.0);
    
    // Determinar qué cara es, basado en la componente con mayor magnitud
    float absX = abs(pos.x);
    float absY = abs(pos.y);
    float absZ = abs(pos.z);
    
    if (absX > absY && absX > absZ)
        normal = float3(sign(pos.x), 0.0, 0.0);
    else if (absY > absX && absY > absZ)
        normal = float3(0.0, sign(pos.y), 0.0);
    else
        normal = float3(0.0, 0.0, sign(pos.z));
    
    return normal;
}

void main(in VSInput VSIn, out PSInput PSIn)
{
    // Construir la matriz de instancia
    float4x4 InstanceMat;
    InstanceMat[0] = VSIn.MtrxRow0;
    InstanceMat[1] = VSIn.MtrxRow1;
    InstanceMat[2] = VSIn.MtrxRow2;
    InstanceMat[3] = VSIn.MtrxRow3;
    
    // Calcular la normal en espacio de objeto
    float3 objectNormal = CalculateNormal(VSIn.Pos);
    
    // Aplicar rotación global
    float4 rotatedPos = mul(float4(VSIn.Pos, 1.0), g_Rotation);
    
    // Calcular posición en espacio de mundo
    float4 worldPos = mul(rotatedPos, InstanceMat);
    
    // Transformar normal al espacio de mundo (ignorando traslación)
    float3x3 rotMat = (float3x3)g_Rotation;
    float3 worldNormal = mul(objectNormal, rotMat);
    
    // Calcular posición final en espacio de clip
    PSIn.Pos = mul(worldPos, g_ViewProj);
    
    // Pasar coordenadas UV y selector de textura
    PSIn.UV = VSIn.UV;
    PSIn.TexSelector = VSIn.TexSelector;
    
    // Pasar normal y posición en espacio de mundo
    PSIn.Normal = worldNormal;
    PSIn.WorldPos = worldPos.xyz;
}
