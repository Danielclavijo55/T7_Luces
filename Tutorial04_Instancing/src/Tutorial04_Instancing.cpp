/*
 *  Copyright 2019-2024 Diligent Graphics LLC
 *  Copyright 2015-2019 Egor Yusov
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *  In no event and under no legal theory, whether in tort (including negligence),
 *  contract, or otherwise, unless required by applicable law (such as deliberate
 *  and grossly negligent acts) or agreed to in writing, shall any Contributor be
 *  liable for any damages, including any direct, indirect, special, incidental,
 *  or consequential damages of any character arising as a result of this License or
 *  out of the use or inability to use the software (including but not limited to damages
 *  for loss of goodwill, work stoppage, computer failure or malfunction, or any and
 *  all other commercial damages or losses), even if such Contributor has been advised
 *  of the possibility of such damages.
 */

#include <random>

#include "Tutorial04_Instancing.hpp"
#include "MapHelper.hpp"
#include "GraphicsUtilities.h"
#include "TextureUtilities.h"
#include "ColorConversion.h"
#include "../../Common/src/TexturedCube.hpp"
#include "imgui.h"

#ifdef PLATFORM_WIN32
#   include <Windows.h>
#endif

namespace Diligent
{

struct InstanceDataType
{
    float4x4 Transform;
    float    TexSelector; // 0 para mezcla 1-2, 1 para mezcla 1-3, 2 para mezcla 1-4
};

SampleBase* CreateSample()
{
    return new Tutorial04_Instancing();
}

void Tutorial04_Instancing::CreatePipelineState()
{
    // Liberar referencias existentes para evitar fugas de memoria
    m_pPSO.Release();
    m_SRB.Release();

    // Define vertex shader input layout
    LayoutElement LayoutElems[] =
    {
        // Per-vertex data - first buffer slot
        // Attribute 0 - vertex position
        LayoutElement{0, 0, 3, VT_FLOAT32, False},
        // Attribute 1 - texture coordinates
        LayoutElement{1, 0, 2, VT_FLOAT32, False},
            
        // Per-instance data - second buffer slot
        // We will use four attributes to encode instance-specific 4x4 transformation matrix
        // Attribute 2 - first row
        LayoutElement{2, 1, 4, VT_FLOAT32, False, INPUT_ELEMENT_FREQUENCY_PER_INSTANCE},
        // Attribute 3 - second row
        LayoutElement{3, 1, 4, VT_FLOAT32, False, INPUT_ELEMENT_FREQUENCY_PER_INSTANCE},
        // Attribute 4 - third row
        LayoutElement{4, 1, 4, VT_FLOAT32, False, INPUT_ELEMENT_FREQUENCY_PER_INSTANCE},
        // Attribute 5 - fourth row
        LayoutElement{5, 1, 4, VT_FLOAT32, False, INPUT_ELEMENT_FREQUENCY_PER_INSTANCE},
        // Selector de textura
        LayoutElement{6, 1, 1, VT_FLOAT32, False, INPUT_ELEMENT_FREQUENCY_PER_INSTANCE}
    };

    // Create a shader source stream factory to load shaders from files.
    RefCntAutoPtr<IShaderSourceInputStreamFactory> pShaderSourceFactory;
    m_pEngineFactory->CreateDefaultShaderSourceStreamFactory(nullptr, &pShaderSourceFactory);

    TexturedCube::CreatePSOInfo CubePsoCI;
    CubePsoCI.pDevice                = m_pDevice;
    CubePsoCI.RTVFormat              = m_pSwapChain->GetDesc().ColorBufferFormat;
    CubePsoCI.DSVFormat              = m_pSwapChain->GetDesc().DepthBufferFormat;
    CubePsoCI.pShaderSourceFactory   = pShaderSourceFactory;
    // Usar los nuevos shaders con iluminación
    CubePsoCI.VSFilePath             = "cube_inst_lighting.vsh";
    CubePsoCI.PSFilePath             = "cube_inst_lighting.psh";
    CubePsoCI.ExtraLayoutElements    = LayoutElems;
    CubePsoCI.NumExtraLayoutElements = _countof(LayoutElems);

    m_pPSO = TexturedCube::CreatePipelineState(CubePsoCI, m_ConvertPSOutputToGamma);

    // Create dynamic uniform buffer that will store our transformation matrix
    // Dynamic buffers can be frequently updated by the CPU
    m_VSConstants.Release();
    m_PSConstants.Release();
    CreateUniformBuffer(m_pDevice, sizeof(float4x4) * 2 + sizeof(float4) * 2, "VS constants CB", &m_VSConstants);
    CreateUniformBuffer(m_pDevice, sizeof(float) + sizeof(float4) * 5 + sizeof(float) * 2 + sizeof(float4x4), "PS constants CB", &m_PSConstants);
    
    // Since we did not explicitly specify the type for 'Constants' variable, default
    // type (SHADER_RESOURCE_VARIABLE_TYPE_STATIC) will be used. Static variables
    // never change and are bound directly to the pipeline state object.
    if (m_pPSO)
    {
        m_pPSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "Constants")->Set(m_VSConstants);
        m_pPSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "PSConstants")->Set(m_PSConstants);
        
        // Since we are using mutable variable, we must create a shader resource binding object
        m_pPSO->CreateShaderResourceBinding(&m_SRB, true);
    }
}

void Tutorial04_Instancing::CreateInstanceBuffer()
{
    // Create instance data buffer that will store transformation matrices and instance IDs
    BufferDesc InstBuffDesc;
    InstBuffDesc.Name = "Instance data buffer";
    // Use default usage as this buffer will only be updated when grid size changes
    InstBuffDesc.Usage     = USAGE_DEFAULT;
    InstBuffDesc.BindFlags = BIND_VERTEX_BUFFER;
    // Incluir espacio para matriz de transformación y selector de textura
    InstBuffDesc.Size      = sizeof(float4x4) * MaxInstances + sizeof(float) * MaxInstances;
    m_pDevice->CreateBuffer(InstBuffDesc, nullptr, &m_InstanceBuffer);
    PopulateInstanceBuffer();
}

// Manejo de eventos nativos (mouse, teclado, etc.)
bool Tutorial04_Instancing::HandleNativeMessage(const void* pNativeMsgData)
{
#ifdef PLATFORM_WIN32
    const MSG* pMsg = reinterpret_cast<const MSG*>(pNativeMsgData);
    
    if (pMsg->message == WM_MOUSEMOVE ||
        pMsg->message == WM_LBUTTONDOWN ||
        pMsg->message == WM_LBUTTONUP ||
        pMsg->message == WM_MOUSEWHEEL)
    {
        int x = LOWORD(pMsg->lParam);
        int y = HIWORD(pMsg->lParam);
        bool buttonDown = (pMsg->message == WM_LBUTTONDOWN);
        bool buttonUp = (pMsg->message == WM_LBUTTONUP);
        int wheel = 0;
        
        if (pMsg->message == WM_MOUSEWHEEL)
        {
            wheel = GET_WHEEL_DELTA_WPARAM(pMsg->wParam) / WHEEL_DELTA;
        }
        
        HandleMouseEvent(x, y, buttonDown, buttonUp, wheel);
        return true; // Mensaje procesado
    }
    
#endif
    return false; // Mensaje no procesado
}

// Actualización de matrices de cámara
void Tutorial04_Instancing::UpdateCameraMatrices()
{
    // Ventana 1: Paneo y Zoom
    ViewWindow1 = float4x4::Translation(CameraWindow1.PanOffset.x, CameraWindow1.PanOffset.y, 0.0f) *
                  float4x4::Scale(CameraWindow1.Zoom, CameraWindow1.Zoom, CameraWindow1.Zoom) *
                  float4x4::RotationX(-0.8f) *
                  float4x4::Translation(0.f, 0.f, 20.0f);
    
    // Ventana 2: Control Orbital
    // Añadimos una matriz de escala con Y negativo para voltear el móvil verticalmente
    ViewWindow2 = float4x4::Translation(0.0f, 0.0f, -CameraWindow2.OrbitDistance) *
                  float4x4::RotationX(CameraWindow2.OrbitAngleX) *
                  float4x4::RotationY(CameraWindow2.OrbitAngleY) *
                  float4x4::Scale(1.0f, -1.0f, 1.0f); // Invierte el eje Y para corregir la orientación
    
    // Ventana 3: Cámara Libre con distancia aumentada
    float4x4 rotation = float4x4::RotationZ(CameraWindow3.RotZ) *
                        float4x4::RotationY(CameraWindow3.RotY) *
                        float4x4::RotationX(CameraWindow3.RotX);
    
    // Aplicamos un factor de escala extremadamente pequeño para alejar radicalmente la vista
    ViewWindow3 = rotation *
                  float4x4::Scale(CameraWindow3.ViewZoom, CameraWindow3.ViewZoom, CameraWindow3.ViewZoom) *
                  float4x4::Translation(-CameraWindow3.Position.x,
                                       -CameraWindow3.Position.y,
                                       -CameraWindow3.Position.z);
}

// Manejo de eventos de ratón para controles de cámara
void Tutorial04_Instancing::HandleMouseEvent(int x, int y, bool buttonDown, bool buttonUp, int wheel)
{
    const auto& SCDesc = m_pSwapChain->GetDesc();
    float screenPosX = static_cast<float>(x) / SCDesc.Width;
    
    // Determinar en qué ventana está el ratón
    int windowIdx = -1;
    if (screenPosX < 1.0f/3.0f)
        windowIdx = 0; // Ventana 1 (Paneo y Zoom)
    else if (screenPosX < 2.0f/3.0f)
        windowIdx = 1; // Ventana 2 (Control Orbital)
    else
        windowIdx = 2; // Ventana 3 (Cámara Libre)
    
    // Capturar/soltar ratón
    if (buttonDown)
    {
        m_MouseCaptured = true;
        m_ActiveWindow = windowIdx;
        m_LastMousePos = float2(static_cast<float>(x), static_cast<float>(y));
    }
    else if (buttonUp)
    {
        m_MouseCaptured = false;
        m_ActiveWindow = -1;
    }
    
    // Si tenemos el ratón capturado, procesar según la ventana activa
    if (m_MouseCaptured)
    {
        float2 currentPos = float2(static_cast<float>(x), static_cast<float>(y));
        float2 delta = currentPos - m_LastMousePos;
        
        switch(m_ActiveWindow)
        {
            case 0: // Ventana 1: Paneo
                // Convertir el movimiento del ratón a desplazamiento de paneo
                CameraWindow1.PanOffset.x += delta.x * 0.01f;
                CameraWindow1.PanOffset.y -= delta.y * 0.01f; // Invertir Y porque en pantalla Y crece hacia abajo
                break;
                
            case 1: // Ventana 2: Control Orbital
                // Convertir el movimiento del ratón a rotación orbital
                CameraWindow2.OrbitAngleY += delta.x * 0.01f;
                CameraWindow2.OrbitAngleX += delta.y * 0.01f;
                break;
                
            case 2: // Ventana 3: Cámara Libre
                // Rotación con mouse para cámara libre
                if (delta.x != 0.0f)
                    CameraWindow3.RotY += delta.x * 0.01f;
                if (delta.y != 0.0f)
                    CameraWindow3.RotX += delta.y * 0.01f;
                break;
        }
        
        m_LastMousePos = currentPos;
    }
    
    // Procesar la rueda del ratón para zoom/distancia
    if (wheel != 0)
    {
        if (windowIdx == 0) // Zoom en ventana 1
        {
            // Zoom in/out según la dirección de la rueda
            CameraWindow1.Zoom += static_cast<float>(wheel) * 0.1f;
            // Limitar el zoom a valores razonables
            CameraWindow1.Zoom = std::max(0.1f, std::min(CameraWindow1.Zoom, 5.0f));
        }
        else if (windowIdx == 1) // Ajustar distancia orbital en ventana 2
        {
            CameraWindow2.OrbitDistance -= static_cast<float>(wheel) * 1.0f;
            CameraWindow2.OrbitDistance = std::max(5.0f, std::min(CameraWindow2.OrbitDistance, 40.0f));
        }
        else if (windowIdx == 2) // Ajustar factor de zoom para ventana 3
        {
            if (wheel > 0)
                CameraWindow3.ViewZoom *= 1.2f; // Acercar
            else
                CameraWindow3.ViewZoom *= 0.8f; // Alejar
                
            // Limitar el zoom a valores razonables
            CameraWindow3.ViewZoom = std::max(0.001f, std::min(CameraWindow3.ViewZoom, 0.1f));
        }
    }
}

void Tutorial04_Instancing::Initialize(const SampleInitInfo& InitInfo)
{
    SampleBase::Initialize(InitInfo);

    // Liberar referencias existentes para evitar fugas de memoria
    m_pPSO.Release();
    m_SRB.Release();
    m_CubeVertexBuffer.Release();
    m_CubeIndexBuffer.Release();
    m_TextureSRV.Release();
    m_TextureDetailSRV.Release();
    m_TextureBlendSRV.Release();
    m_TextureAltSRV.Release();
    m_ShadowMapSRV.Release();
    m_InstanceBuffer.Release();

    // Crear recursos de iluminación y sombras
    CreateLightingBuffers();
    CreateShadowMap();
    CreateShadowMapPSO();
    CreateFloor();
    CreateFloorTexture();
    CreateFloorPSO();

    // Crear pipeline state y recursos del cubo
    CreatePipelineState();

    // Load textured cube
    m_CubeVertexBuffer = TexturedCube::CreateVertexBuffer(m_pDevice, GEOMETRY_PRIMITIVE_VERTEX_FLAG_POS_TEX);
    m_CubeIndexBuffer  = TexturedCube::CreateIndexBuffer(m_pDevice);
    
    // Cargar todas las texturas necesarias para el multitexturing
    m_TextureSRV = TexturedCube::LoadTexture(m_pDevice, "DGLogo.png")->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    m_TextureDetailSRV = TexturedCube::LoadTexture(m_pDevice, "BrickWall.jpg")->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    m_TextureBlendSRV = TexturedCube::LoadTexture(m_pDevice, "BlendMap.png")->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    m_TextureAltSRV = TexturedCube::LoadTexture(m_pDevice, "MetalPlate.jpg")->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
   
    // Set cube texture SRV in the SRB - Vinculamos todas las texturas
    if (m_SRB)
    {
        m_SRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_Texture")->Set(m_TextureSRV);
        m_SRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_TextureDetail")->Set(m_TextureDetailSRV);
        m_SRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_TextureBlend")->Set(m_TextureBlendSRV);
        m_SRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_TextureAlt")->Set(m_TextureAltSRV);
        
        // Comentar esta línea que podría causar problemas
        //m_SRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_ShadowMap")->Set(m_ShadowMapSRV);
    }

    CreateInstanceBuffer();
    
    // Inicializar las vistas de cámara
    ViewWindow1 = float4x4::RotationX(-0.8f) * float4x4::Translation(0.f, 0.f, 20.0f);
    ViewWindow2 = float4x4::RotationX(-0.8f) * float4x4::Translation(0.f, 0.f, 20.0f);
    ViewWindow3 = float4x4::RotationX(-0.8f) * float4x4::Translation(0.f, 0.f, 20.0f);
    
    // Inicializar parámetros de cámara
    CameraWindow1.Zoom = 1.0f;
    
    // Inicializar parámetros para ventana orbital con valores que muestren el móvil correctamente
    CameraWindow2.OrbitAngleX = 3.0f;  // Valor ajustado según la imagen donde se ve correctamente
    CameraWindow2.OrbitAngleY = 0.0f;
    CameraWindow2.OrbitDistance = 20.0f;
    
    // Inicializar parámetros para cámara libre (Ventana 3) - valores exactos de la imagen final
    CameraWindow3.Position = float3(-0.77f, 0.83f, -4.57f);
    CameraWindow3.RotX = -1.43f;
    CameraWindow3.RotY = 0.05f;
    CameraWindow3.RotZ = 0.05f;
    CameraWindow3.ViewZoom = 0.226f; // Valor exacto de la imagen
}

static float blendFactor = 0.5f;
static float specularPower = 32.0f;
static float specularIntensity = 0.5f;
static float3 lightDir = float3(-0.577f, -0.577f, -0.577f);
static float4 lightColor = float4(1.0f, 1.0f, 1.0f, 1.0f);
static float4 ambientColor = float4(0.1f, 0.1f, 0.1f, 1.0f);

void Tutorial04_Instancing::UpdateUI()
{
    // Actualizar matrices de cámara basadas en parámetros actuales
    UpdateCameraMatrices();
    
    // Ventana 1: Paneo y Zoom
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300, 150), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Controles", nullptr))
      {
          ImGui::Text("Parámetros de iluminación");
          ImGui::SliderFloat("Texture Blend", &blendFactor, 0.0f, 1.0f);
          ImGui::SliderFloat("Specular Power", &specularPower, 1.0f, 128.0f);
          ImGui::SliderFloat("Specular Intensity", &specularIntensity, 0.0f, 1.0f);
          
          ImGui::Text("Dirección de la luz");
          ImGui::SliderFloat("Light X", &lightDir.x, -1.0f, 1.0f);
          ImGui::SliderFloat("Light Y", &lightDir.y, -1.0f, 1.0f);
          ImGui::SliderFloat("Light Z", &lightDir.z, -1.0f, 1.0f);
          
          if (ImGui::ColorEdit3("Light Color", &lightColor.r))
          {
              lightColor.a = 1.0f;
          }
          
          if (ImGui::ColorEdit3("Ambient Color", &ambientColor.r))
          {
              ambientColor.a = 1.0f;
          }
      }
      ImGui::End();
    
    ImGui::SliderFloat("Texture Blend Factor", &blendFactor, 0.0f, 1.0f);
    if (ImGui::Begin("Ventana 1: Paneo y Zoom", nullptr))
    {
        ImGui::Text("Arrastre con el ratón para paneo");
        ImGui::Text("Use la rueda del ratón para zoom");
        
        // Controladores de paneo
        ImGui::SliderFloat("Pan X", &CameraWindow1.PanOffset.x, -10.0f, 10.0f);
        ImGui::SliderFloat("Pan Y", &CameraWindow1.PanOffset.y, -10.0f, 10.0f);
        
        // Controlador de zoom
        ImGui::SliderFloat("Zoom", &CameraWindow1.Zoom, 0.1f, 5.0f);
        
        if (ImGui::Button("Reset Camera"))
        {
            CameraWindow1.PanOffset = float2(0.0f, 0.0f);
            CameraWindow1.Zoom = 1.0f;
        }
    }
    ImGui::End();
    
    // Ventana 2: Control Orbital
    ImGui::SetNextWindowPos(ImVec2(320, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300, 150), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Ventana 2: Control Orbital", nullptr))
    {
        ImGui::Text("Arrastre con el ratón para orbitar");
        
        // Controladores de ángulos de órbita
        ImGui::SliderFloat("Orbit X", &CameraWindow2.OrbitAngleX, -PI_F, PI_F);
        ImGui::SliderFloat("Orbit Y", &CameraWindow2.OrbitAngleY, -PI_F, PI_F);
        ImGui::SliderFloat("Distance", &CameraWindow2.OrbitDistance, 5.0f, 40.0f);
        
        if (ImGui::Button("Reset Orbit"))
        {
            CameraWindow2.OrbitAngleX = 3.0f; // Valor ajustado según la imagen donde funciona
            CameraWindow2.OrbitAngleY = 0.0f;
            CameraWindow2.OrbitDistance = 20.0f;
        }
    }
    ImGui::End();
    
    // Ventana 3: Cámara Libre
    ImGui::SetNextWindowPos(ImVec2(630, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300, 200), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Ventana 3: Cámara Libre", nullptr))
    {
        ImGui::Text("Control de posición de cámara:");
        
        // Controles de posición
        ImGui::Text("Posición:");
        ImGui::SliderFloat("X", &CameraWindow3.Position.x, -15.0f, 15.0f);
        ImGui::SliderFloat("Y", &CameraWindow3.Position.y, -15.0f, 15.0f);
        ImGui::SliderFloat("Z", &CameraWindow3.Position.z, -40.0f, 40.0f);
        
        ImGui::Separator();
        
        // Controles de rotación
        ImGui::Text("Rotación:");
        ImGui::SliderFloat("Rot X", &CameraWindow3.RotX, -PI_F, PI_F);
        ImGui::SliderFloat("Rot Y", &CameraWindow3.RotY, -PI_F, PI_F);
        ImGui::SliderFloat("Rot Z", &CameraWindow3.RotZ, -PI_F, PI_F);
        
        // Control de escala para el zoom
        ImGui::SliderFloat("Zoom", &CameraWindow3.ViewZoom, 0.01f, 0.5f, "%.3f");
    }
    ImGui::End();
}

void Tutorial04_Instancing::PopulateInstanceBuffer()
{
    std::vector<InstanceDataType> InstanceDataArray(MaxInstances);
    int instId = 0;

    // Ángulos de rotación para diferentes partes
    static float mainRotation = 0.0f;          // Rotación principal del móvil
    static float firstTierRotation = 0.0f;     // Rotación del primer nivel
    static float secondTierRotation = 0.0f;    // Rotación del segundo nivel

    // Actualizar ángulos con velocidades diferenciadas
    mainRotation += 0.003f;                    // Rotación base más lenta
    firstTierRotation += 0.005f;               // Primer nivel gira un poco más rápido
    secondTierRotation += 0.007f;              // Segundo nivel gira más rápido aún

    // Base principal (placa superior)
    float4x4 baseMatrix = float4x4::Scale(1.6f, 0.1f, 1.6f) * float4x4::Translation(0.0f, 4.8f, 0.0f);
    InstanceDataArray[instId].Transform = baseMatrix;
    InstanceDataArray[instId].TexSelector = 0.0f;  // Mezcla 1-2
    instId++;

    // Matrices de rotación para los diferentes niveles
    float4x4 mainRotMatrix = float4x4::RotationY(mainRotation);
    float4x4 firstLevelMatrix = mainRotMatrix * float4x4::RotationY(firstTierRotation);
    float4x4 secondLevelMatrix = firstLevelMatrix * float4x4::RotationY(secondTierRotation);

    // === PRIMER NIVEL ===
    // Palo central vertical
    float4x4 centerPoleMatrix = float4x4::Scale(0.1f, 1.0f, 0.1f) * float4x4::Translation(0.0f, 3.65f, 0.0f);
    InstanceDataArray[instId].Transform = centerPoleMatrix;
    InstanceDataArray[instId].TexSelector = 1.0f;  // Mezcla 1-3
    instId++;

    // Brazos horizontales del primer nivel
    float4x4 horizontalArm1 = float4x4::Scale(3.6f, 0.1f, 0.1f) *
                             float4x4::Translation(0.0f, 2.6f, 0.0f) *
                             firstLevelMatrix;
    float4x4 horizontalArm2 = float4x4::Scale(0.1f, 0.1f, 3.6f) *
                             float4x4::Translation(0.0f, 2.6f, 0.0f) *
                             firstLevelMatrix;
    
    InstanceDataArray[instId].Transform = horizontalArm1;
    InstanceDataArray[instId].TexSelector = 1.0f;  // Mezcla 1-3
    instId++;
    
    InstanceDataArray[instId].Transform = horizontalArm2;
    InstanceDataArray[instId].TexSelector = 1.0f;  // Mezcla 1-3
    instId++;

    // Cubos del primer nivel
    float4x4 cubePositions[] = {
        float4x4::Translation(3.0f, 2.0f, 0.0f),
        float4x4::Translation(-3.0f, 2.0f, 0.0f),
        float4x4::Translation(0.0f, 2.0f, 3.0f),
        float4x4::Translation(0.0f, 2.0f, -3.0f)
    };

    // Alternamos entre diferentes mezclas para los cubos
    float cubeSelectors[] = {0.0f, 1.0f, 2.0f, 0.0f};

    for (int i = 0; i < static_cast<int>(sizeof(cubePositions)/sizeof(cubePositions[0])); i++) {
        float4x4 cubeMatrix = float4x4::Scale(0.6f, 0.6f, 0.6f) * cubePositions[i] * firstLevelMatrix;
        InstanceDataArray[instId].Transform = cubeMatrix;
        InstanceDataArray[instId].TexSelector = cubeSelectors[i % 4];
        instId++;
    }

    // === SEGUNDO NIVEL ===
    // Palos verticales conectores
    float4x4 verticalConnectors[] = {
        float4x4::Scale(0.1f, 0.85f, 0.1f) * float4x4::Translation(0.0f, 0.85f, 3.0f),
        float4x4::Scale(0.1f, 0.85f, 0.1f) * float4x4::Translation(0.0f, 0.85f, -3.0f),
        float4x4::Scale(0.1f, 0.85f, 0.1f) * float4x4::Translation(3.0f, 0.85f, 0.0f),
        float4x4::Scale(0.1f, 0.85f, 0.1f) * float4x4::Translation(-3.0f, 0.85f, 0.0f)
    };

    for (int i = 0; i < static_cast<int>(sizeof(verticalConnectors)/sizeof(verticalConnectors[0])); i++) {
        InstanceDataArray[instId].Transform = verticalConnectors[i] * firstLevelMatrix;
        InstanceDataArray[instId].TexSelector = 1.0f;  // Mezcla 1-3
        instId++;
    }

    // Brazos horizontales del segundo nivel
    float4x4 secondLevelArms[] = {
        float4x4::Scale(2.0f, 0.1f, 0.1f) * float4x4::Translation(0.0f, 0.2f, 3.0f),
        float4x4::Scale(2.0f, 0.1f, 0.1f) * float4x4::Translation(0.0f, 0.2f, -3.0f),
        float4x4::Scale(0.1f, 0.1f, 2.0f) * float4x4::Translation(3.0f, 0.2f, 0.0f),
        float4x4::Scale(0.1f, 0.1f, 2.0f) * float4x4::Translation(-3.0f, 0.2f, 0.0f)
    };

    for (int i = 0; i < static_cast<int>(sizeof(secondLevelArms)/sizeof(secondLevelArms[0])); i++) {
        InstanceDataArray[instId].Transform = secondLevelArms[i] * secondLevelMatrix;
        InstanceDataArray[instId].TexSelector = 1.0f;  // Mezcla 1-3
        instId++;
    }

    // Cubos del segundo nivel
    float4x4 secondTierPositions[] = {
        float4x4::Translation(1.0f, -0.4f, 3.0f),
        float4x4::Translation(-1.0f, -0.4f, 3.0f),
        float4x4::Translation(1.0f, -0.4f, -3.0f),
        float4x4::Translation(-1.0f, -0.4f, -3.0f),
        float4x4::Translation(3.0f, -0.4f, 1.0f),
        float4x4::Translation(3.0f, -0.4f, -1.0f),
        float4x4::Translation(-3.0f, -0.4f, 1.0f),
        float4x4::Translation(-3.0f, -0.4f, -1.0f)
    };

    for (int i = 0; i < static_cast<int>(sizeof(secondTierPositions)/sizeof(secondTierPositions[0])); i++) {
        float4x4 cubeMatrix = float4x4::Scale(0.6f, 0.6f, 0.6f) * secondTierPositions[i] * secondLevelMatrix;
        InstanceDataArray[instId].Transform = cubeMatrix;
        // Alternamos las mezclas para los cubos del segundo nivel
        InstanceDataArray[instId].TexSelector = i % 3;  // Alternamos entre 0, 1 y 2
        instId++;
    }

    // Actualizar el buffer
    Uint32 DataSize = static_cast<Uint32>(sizeof(InstanceDataType) * instId);
    m_pImmediateContext->UpdateBuffer(m_InstanceBuffer, 0, DataSize, InstanceDataArray.data(),
                                  RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
}

void Tutorial04_Instancing::Update(double CurrTime, double ElapsedTime)
{
    SampleBase::Update(CurrTime, ElapsedTime);
    
    // Actualizar constante del pixel shader para la mezcla de texturas y propiedades de iluminación
    {
        MapHelper<float> PSConstants(m_pImmediateContext, m_PSConstants, MAP_WRITE, MAP_FLAG_DISCARD);
        PSConstants[0] = blendFactor;
        
        // Resto de las propiedades de iluminación
        float4* pLightDir = reinterpret_cast<float4*>(&PSConstants[1]);
        *pLightDir = float4(normalize(lightDir), 0.0f);
        
        float4* pLightColor = reinterpret_cast<float4*>(&PSConstants[5]);
        *pLightColor = lightColor;
        
        float4* pAmbientColor = reinterpret_cast<float4*>(&PSConstants[9]);
        *pAmbientColor = ambientColor;
        
        // Para el cálculo de iluminación, necesitamos la posición de la cámara
        float3 cameraPos = float3(0.0f, 0.0f, 0.0f);
        switch(m_ActiveWindow)
        {
            case 0: // Ventana 1
                cameraPos = float3(CameraWindow1.PanOffset.x, CameraWindow1.PanOffset.y, 20.0f / CameraWindow1.Zoom);
                break;
            case 1: // Ventana 2
            {
                // Calcular dirección usando trigonometría directamente
                float3 dir;
                dir.x = -sin(CameraWindow2.OrbitAngleY) * cos(CameraWindow2.OrbitAngleX);
                dir.y = -sin(CameraWindow2.OrbitAngleX);
                dir.z = -cos(CameraWindow2.OrbitAngleY) * cos(CameraWindow2.OrbitAngleX);
                dir = normalize(dir);
                cameraPos = -dir * CameraWindow2.OrbitDistance;
            }
                break;
            case 2: // Ventana 3
                cameraPos = CameraWindow3.Position;
                break;
            default:
                cameraPos = float3(0.0f, 0.0f, 20.0f);
        }
        
        float4* pCameraPos = reinterpret_cast<float4*>(&PSConstants[13]);
        *pCameraPos = float4(cameraPos, 1.0f);
        
        // Propiedades especulares
        float* pSpecularPower = reinterpret_cast<float*>(&PSConstants[17]);
        *pSpecularPower = specularPower;
        
        float* pSpecularIntensity = reinterpret_cast<float*>(&PSConstants[18]);
        *pSpecularIntensity = specularIntensity;
        
        // Matriz de view-projection de la luz para sombras
        float4x4* pLightViewProj = reinterpret_cast<float4x4*>(&PSConstants[19]);
        *pLightViewProj = m_LightViewProjMatrix;
    }
    
    // Actualizar también los atributos de luz para el suelo
    {
        MapHelper<float4> LightAttribs(m_pImmediateContext, m_LightAttribs, MAP_WRITE, MAP_FLAG_DISCARD);
        LightAttribs[0] = float4(normalize(lightDir), 0.0f);
        LightAttribs[1] = lightColor;
        LightAttribs[2] = ambientColor;
        
        // Posición de la cámara
        float3 cameraPos = float3(0.0f, 0.0f, 0.0f);
        if (m_ActiveWindow >= 0 && m_ActiveWindow < 3)
        {
            switch(m_ActiveWindow)
            {
                case 0: cameraPos = float3(CameraWindow1.PanOffset.x, CameraWindow1.PanOffset.y, 20.0f / CameraWindow1.Zoom); break;
                case 1:
                {
                    float3 dir = float3(0.0f, 0.0f, -1.0f);
                    float4x4 rotMat = float4x4::RotationX(CameraWindow2.OrbitAngleX) * float4x4::RotationY(CameraWindow2.OrbitAngleY);
                    float4 dir4 = rotMat * float4(dir, 0.0f);
                    dir = normalize(float3(dir4.x, dir4.y, dir4.z));
                    cameraPos = -dir * CameraWindow2.OrbitDistance;
                }
                break;
                case 2: cameraPos = CameraWindow3.Position; break;
            }
        }
        else
        {
            cameraPos = float3(0.0f, 0.0f, 20.0f);
        }
        
        LightAttribs[3] = float4(cameraPos, 1.0f);
        
        // Propiedades especulares
        float* pSpecularPower = reinterpret_cast<float*>(&LightAttribs[4]);
        *pSpecularPower = specularPower;
        
        float* pSpecularIntensity = reinterpret_cast<float*>(pSpecularPower + 1);
        *pSpecularIntensity = specularIntensity;
        
        // Matriz de view-projection de la luz para sombras
        float4x4* pLightViewProj = reinterpret_cast<float4x4*>(&LightAttribs[5]);
        *pLightViewProj = m_LightViewProjMatrix;
    }
    
    UpdateUI();
    
    CalculateLightViewProj();

    // Get pretransform matrix that rotates the scene according the surface orientation
    auto SrfPreTransform = GetSurfacePretransformMatrix(float3{0, 0, 1});

    // Get projection matrix adjusted to the current screen orientation
    auto Proj = GetAdjustedProjectionMatrix(PI_F / 4.0f, 0.1f, 100.f);

    // Se usa ViewWindow1 como matriz de vista predeterminada
    m_ViewProjMatrix = ViewWindow1 * SrfPreTransform * Proj;

    // Global rotation matrix
    m_RotationMatrix = float4x4::RotationY(static_cast<float>(CurrTime) * 0.1f) *
                            float4x4::RotationX(-static_cast<float>(CurrTime) * 0.05f);
    
    // Actualizar la matriz de transformación del suelo
    {
        MapHelper<float4x4> FloorTransform(m_pImmediateContext, m_FloorTransform, MAP_WRITE, MAP_FLAG_DISCARD);
        FloorTransform[0] = float4x4::Identity(); // Matriz de modelo
        FloorTransform[1] = m_ViewProjMatrix;     // Matriz de vista-proyección
    }
}

void Tutorial04_Instancing::CalculateLightViewProj()
{
    // Calcular una matriz de vista-proyección para la luz simplificada
    
    // Normalizar la dirección de la luz
    float3 lightDir = normalize(float3(m_LightDirection.x, m_LightDirection.y, m_LightDirection.z));
    float3 lightPos = -lightDir * 30.0f; // Posición de la luz a 30 unidades en dirección opuesta
    
    // Crear manualmente una matriz de vista basada en la posición de la luz
    float4x4 lightView = float4x4::Translation(-lightPos.x, -lightPos.y, -lightPos.z);
    
    // Construir manualmente una matriz de proyección ortográfica
    // Parámetros para una caja de vista ortográfica
    float size = 20.0f;
    float near = 0.1f;
    float far = 100.0f;
    
    // Construir la matriz de proyección ortográfica manualmente
    float4x4 lightProj;
    lightProj._11 = 1.0f / size;
    lightProj._12 = 0.0f;
    lightProj._13 = 0.0f;
    lightProj._14 = 0.0f;
    
    lightProj._21 = 0.0f;
    lightProj._22 = 1.0f / size;
    lightProj._23 = 0.0f;
    lightProj._24 = 0.0f;
    
    lightProj._31 = 0.0f;
    lightProj._32 = 0.0f;
    lightProj._33 = 2.0f / (far - near);
    lightProj._34 = 0.0f;
    
    lightProj._41 = 0.0f;
    lightProj._42 = 0.0f;
    lightProj._43 = -(far + near) / (far - near);
    lightProj._44 = 1.0f;
    
    // Combinar vista y proyección
    m_LightViewProjMatrix = lightView * lightProj;
}

void Tutorial04_Instancing::CreateLightingBuffers()
{
    // Crear buffer para atributos de luz
    BufferDesc BuffDesc;
    BuffDesc.Name = "Light attributes buffer";
    BuffDesc.Size = sizeof(float4) * 4 + sizeof(float) * 2; // LightDir, LightColor, AmbientColor, CameraPos, SpecularPower, SpecularIntensity
    BuffDesc.Usage = USAGE_DYNAMIC;
    BuffDesc.BindFlags = BIND_UNIFORM_BUFFER;
    BuffDesc.CPUAccessFlags = CPU_ACCESS_WRITE;
    m_pDevice->CreateBuffer(BuffDesc, nullptr, &m_LightAttribs);

    // Crear buffer para transformación del suelo
    BuffDesc.Name = "Floor transform buffer";
    BuffDesc.Size = sizeof(float4x4) * 2; // Model, ViewProj
    m_pDevice->CreateBuffer(BuffDesc, nullptr, &m_FloorTransform);
}

void Tutorial04_Instancing::CreateShadowMap()
{
    // Crear textura para el mapa de sombras simplificado
    TextureDesc ShadowMapDesc;
    ShadowMapDesc.Name = "Shadow map";
    ShadowMapDesc.Type = RESOURCE_DIM_TEX_2D;
    ShadowMapDesc.Width = 1024;
    ShadowMapDesc.Height = 1024;
    ShadowMapDesc.Format = TEX_FORMAT_D32_FLOAT;
    ShadowMapDesc.BindFlags = BIND_SHADER_RESOURCE | BIND_DEPTH_STENCIL;
    ShadowMapDesc.MipLevels = 1;

    m_pDevice->CreateTexture(ShadowMapDesc, nullptr, &m_ShadowMap);
    m_ShadowMapDSV = m_ShadowMap->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
    m_ShadowMapSRV = m_ShadowMap->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
}

void Tutorial04_Instancing::CreateShadowMapPSO()
{
    // Liberar referencias existentes para evitar fugas de memoria
    m_pShadowMapPSO.Release();
    m_ShadowMapSRB.Release();
    
    // Crear PSO simplificado para renderizar objetos al mapa de sombras
    GraphicsPipelineStateCreateInfo PSOCreateInfo;
    PipelineStateDesc&              PSODesc = PSOCreateInfo.PSODesc;

    PSODesc.Name = "Shadow map PSO";
    PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

    // Configurar el render target y depth buffer
    GraphicsPipelineDesc& GraphicsPipeline = PSOCreateInfo.GraphicsPipeline;
    GraphicsPipeline.NumRenderTargets = 0; // No render target, solo depth buffer
    GraphicsPipeline.DSVFormat = TEX_FORMAT_D32_FLOAT;
    GraphicsPipeline.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    GraphicsPipeline.RasterizerDesc.CullMode = CULL_MODE_BACK;
    GraphicsPipeline.DepthStencilDesc.DepthEnable = True;
    GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = True;
    
    ShaderCreateInfo ShaderCI;
    ShaderCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
    ShaderCI.Desc.UseCombinedTextureSamplers = true;

    // Crear factory para cargar los shaders
    RefCntAutoPtr<IShaderSourceInputStreamFactory> pShaderSourceFactory;
    m_pEngineFactory->CreateDefaultShaderSourceStreamFactory(nullptr, &pShaderSourceFactory);
    ShaderCI.pShaderSourceStreamFactory = pShaderSourceFactory;

    // Crear vertex shader
    RefCntAutoPtr<IShader> pVS;
    {
        ShaderCI.Desc.ShaderType = SHADER_TYPE_VERTEX;
        ShaderCI.EntryPoint = "main";
        ShaderCI.Desc.Name = "Shadow VS";
        ShaderCI.FilePath = "shadowmap.vsh";
        m_pDevice->CreateShader(ShaderCI, &pVS);
    }

    // Para OpenGL y otros backends, necesitamos un pixel shader, aunque sea vacío
    RefCntAutoPtr<IShader> pPS;
    {
        ShaderCI.Desc.ShaderType = SHADER_TYPE_PIXEL;
        ShaderCI.EntryPoint = "main";
        ShaderCI.Desc.Name = "Shadow PS";
        ShaderCI.FilePath = "shadowmap.psh";
        m_pDevice->CreateShader(ShaderCI, &pPS);
    }

    PSOCreateInfo.pVS = pVS;
    PSOCreateInfo.pPS = pPS;

    // Definir el layout de entrada (mismo que para el cubo instanciado)
    LayoutElement LayoutElems[] =
    {
        // Per-vertex data - first buffer slot
        LayoutElement{0, 0, 3, VT_FLOAT32, False},
        LayoutElement{1, 0, 2, VT_FLOAT32, False},
            
        // Per-instance data - second buffer slot
        LayoutElement{2, 1, 4, VT_FLOAT32, False, INPUT_ELEMENT_FREQUENCY_PER_INSTANCE},
        LayoutElement{3, 1, 4, VT_FLOAT32, False, INPUT_ELEMENT_FREQUENCY_PER_INSTANCE},
        LayoutElement{4, 1, 4, VT_FLOAT32, False, INPUT_ELEMENT_FREQUENCY_PER_INSTANCE},
        LayoutElement{5, 1, 4, VT_FLOAT32, False, INPUT_ELEMENT_FREQUENCY_PER_INSTANCE},
        LayoutElement{6, 1, 1, VT_FLOAT32, False, INPUT_ELEMENT_FREQUENCY_PER_INSTANCE}
    };
    
    GraphicsPipeline.InputLayout.LayoutElements = LayoutElems;
    GraphicsPipeline.InputLayout.NumElements = _countof(LayoutElems);

    // Crear buffer de constantes
    m_VSConstants.Release();
    CreateUniformBuffer(m_pDevice, sizeof(float4x4) * 2, "Shadow VS constants", &m_VSConstants);

    // Definir variables estáticas
    PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;
    
    m_pDevice->CreateGraphicsPipelineState(PSOCreateInfo, &m_pShadowMapPSO);
    
    if (m_pShadowMapPSO)
    {
        m_pShadowMapPSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "Constants")->Set(m_VSConstants);
        m_pShadowMapPSO->CreateShaderResourceBinding(&m_ShadowMapSRB, true);
    }
}

void Tutorial04_Instancing::CreateFloor()
{
    // Crear los vértices del suelo (un plano XZ grande)
    struct FloorVertex
    {
        float3 Pos;
        float2 UV;
    };

    // Definir los vértices del suelo
    FloorVertex FloorVertices[] = {
        {{-50.0f, -5.0f, -50.0f}, {0.0f, 0.0f}},
        {{ 50.0f, -5.0f, -50.0f}, {1.0f, 0.0f}},
        {{ 50.0f, -5.0f,  50.0f}, {1.0f, 1.0f}},
        {{-50.0f, -5.0f,  50.0f}, {0.0f, 1.0f}}
    };

    // Crear buffer de vértices
    BufferDesc VertBuffDesc;
    VertBuffDesc.Name = "Floor vertex buffer";
    VertBuffDesc.Usage = USAGE_IMMUTABLE;
    VertBuffDesc.BindFlags = BIND_VERTEX_BUFFER;
    VertBuffDesc.Size = sizeof(FloorVertices);
    
    BufferData VBData;
    VBData.pData = FloorVertices;
    VBData.DataSize = sizeof(FloorVertices);
    m_pDevice->CreateBuffer(VertBuffDesc, &VBData, &m_FloorVertexBuffer);

    // Definir los índices del suelo
    Uint32 FloorIndices[] = {
        0, 1, 2,
        0, 2, 3
    };

    // Crear buffer de índices
    BufferDesc IndBuffDesc;
    IndBuffDesc.Name = "Floor index buffer";
    IndBuffDesc.Usage = USAGE_IMMUTABLE;
    IndBuffDesc.BindFlags = BIND_INDEX_BUFFER;
    IndBuffDesc.Size = sizeof(FloorIndices);
    
    BufferData IBData;
    IBData.pData = FloorIndices;
    IBData.DataSize = sizeof(FloorIndices);
    m_pDevice->CreateBuffer(IndBuffDesc, &IBData, &m_FloorIndexBuffer);
}

void Tutorial04_Instancing::CreateFloorTexture()
{
    // Crear una textura de suelo procedural simplificada
    const Uint32 TexWidth  = 256; // Reducido para simplificar
    const Uint32 TexHeight = 256;
    std::vector<Uint32> Data(TexWidth * TexHeight);
    
    // Crear un patrón de tablero de ajedrez
    for (Uint32 y = 0; y < TexHeight; ++y)
    {
        for (Uint32 x = 0; x < TexWidth; ++x)
        {
            bool isEvenRow = (y / 32) % 2 == 0; // Tamaño de cuadro reducido
            bool isEvenCol = (x / 32) % 2 == 0;
            
            Uint32 color;
            if (isEvenRow == isEvenCol)
                color = 0xFF808080; // Gris medio
            else
                color = 0xFF404040; // Gris oscuro
                
            Data[y * TexWidth + x] = color;
        }
    }
    
    // Crear descriptor de textura
    TextureDesc TexDesc;
    TexDesc.Type      = RESOURCE_DIM_TEX_2D;
    TexDesc.Width     = TexWidth;
    TexDesc.Height    = TexHeight;
    TexDesc.Format    = TEX_FORMAT_RGBA8_UNORM;
    TexDesc.BindFlags = BIND_SHADER_RESOURCE;
    TexDesc.Name      = "Floor texture";
    
    // Datos de la textura
    TextureSubResData Level0Data;
    Level0Data.pData  = Data.data();
    Level0Data.Stride = TexWidth * 4;
    
    TextureData TexData;
    TexData.NumSubresources = 1;
    TexData.pSubResources   = &Level0Data;
    
    // Crear la textura
    RefCntAutoPtr<ITexture> pFloorTexture;
    m_pDevice->CreateTexture(TexDesc, &TexData, &pFloorTexture);
    m_FloorTextureSRV = pFloorTexture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
}

void Tutorial04_Instancing::CreateFloorPSO()
{
    // Liberar referencias existentes para evitar fugas de memoria
    m_pFloorPSO.Release();
    m_FloorSRB.Release();
    
    GraphicsPipelineStateCreateInfo PSOCreateInfo;
    PipelineStateDesc&              PSODesc = PSOCreateInfo.PSODesc;

    PSODesc.Name = "Floor PSO";
    PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

    GraphicsPipelineDesc& GraphicsPipeline = PSOCreateInfo.GraphicsPipeline;
    GraphicsPipeline.NumRenderTargets = 1;
    GraphicsPipeline.RTVFormats[0] = m_pSwapChain->GetDesc().ColorBufferFormat;
    GraphicsPipeline.DSVFormat = m_pSwapChain->GetDesc().DepthBufferFormat;
    GraphicsPipeline.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    GraphicsPipeline.RasterizerDesc.CullMode = CULL_MODE_BACK;
    GraphicsPipeline.DepthStencilDesc.DepthEnable = True;
    
    ShaderCreateInfo ShaderCI;
    ShaderCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
    ShaderCI.Desc.UseCombinedTextureSamplers = true;

    RefCntAutoPtr<IShaderSourceInputStreamFactory> pShaderSourceFactory;
    m_pEngineFactory->CreateDefaultShaderSourceStreamFactory(nullptr, &pShaderSourceFactory);
    ShaderCI.pShaderSourceStreamFactory = pShaderSourceFactory;

    // Crear vertex shader
    RefCntAutoPtr<IShader> pVS;
    {
        ShaderCI.Desc.ShaderType = SHADER_TYPE_VERTEX;
        ShaderCI.EntryPoint = "main";
        ShaderCI.Desc.Name = "Floor VS";
        ShaderCI.FilePath = "floor.vsh";
        m_pDevice->CreateShader(ShaderCI, &pVS);
    }

    // Crear pixel shader
    RefCntAutoPtr<IShader> pPS;
    {
        ShaderCI.Desc.ShaderType = SHADER_TYPE_PIXEL;
        ShaderCI.EntryPoint = "main";
        ShaderCI.Desc.Name = "Floor PS";
        ShaderCI.FilePath = "floor.psh";
        m_pDevice->CreateShader(ShaderCI, &pPS);
    }

    PSOCreateInfo.pVS = pVS;
    PSOCreateInfo.pPS = pPS;

    // Definir el layout de entrada para el suelo
    LayoutElement FloorLayoutElems[] =
    {
        LayoutElement{0, 0, 3, VT_FLOAT32, False}, // Posición
        LayoutElement{1, 0, 2, VT_FLOAT32, False}  // Coordenadas de textura
    };
    
    GraphicsPipeline.InputLayout.LayoutElements = FloorLayoutElems;
    GraphicsPipeline.InputLayout.NumElements = _countof(FloorLayoutElems);

    // Definir variables de recursos muy simplificadas
    ShaderResourceVariableDesc Vars[] =
    {
        {SHADER_TYPE_PIXEL, "g_FloorTexture", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE}
    };
    
    PSODesc.ResourceLayout.Variables = Vars;
    PSODesc.ResourceLayout.NumVariables = _countof(Vars);
    
    // Definir samplers
    SamplerDesc SamplerDesc;
    SamplerDesc.MinFilter = FILTER_TYPE_LINEAR;
    SamplerDesc.MagFilter = FILTER_TYPE_LINEAR;
    SamplerDesc.MipFilter = FILTER_TYPE_LINEAR;
    SamplerDesc.AddressU = TEXTURE_ADDRESS_WRAP;
    SamplerDesc.AddressV = TEXTURE_ADDRESS_WRAP;
    SamplerDesc.AddressW = TEXTURE_ADDRESS_WRAP;
    
    ImmutableSamplerDesc ImmutableSamplers[] = {
        {SHADER_TYPE_PIXEL, "g_Texture_sampler", SamplerDesc}
    };
    
    PSODesc.ResourceLayout.ImmutableSamplers = ImmutableSamplers;
    PSODesc.ResourceLayout.NumImmutableSamplers = _countof(ImmutableSamplers);
    
    m_pDevice->CreateGraphicsPipelineState(PSOCreateInfo, &m_pFloorPSO);
    
    if (m_pFloorPSO)
    {
        m_pFloorPSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "Constants")->Set(m_FloorTransform);
        m_pFloorPSO->CreateShaderResourceBinding(&m_FloorSRB, true);
        
        if (m_FloorSRB)
        {
            // Asignar solo la textura del suelo, sin referencia al mapa de sombras
 //           m_FloorSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_FloorTexture")->Set(m_FloorTextureSRV);
        }
    }
}

// Render a frame
void Tutorial04_Instancing::Render()
{
    PopulateInstanceBuffer();
    
    // No renderizamos el mapa de sombras para simplificar el proceso
    
    // ======= PASO 1: Renderizar la escena directamente =======
    
    auto* pRTV = m_pSwapChain->GetCurrentBackBufferRTV();
    auto* pDSV = m_pSwapChain->GetDepthBufferDSV();

    // Clear the back buffer
    float4 ClearColor = {0.350f, 0.350f, 0.350f, 1.0f};
    if (m_ConvertPSOutputToGamma)
    {
        ClearColor = LinearToSRGB(ClearColor);
    }
    m_pImmediateContext->ClearRenderTarget(pRTV, ClearColor.Data(), RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    m_pImmediateContext->ClearDepthStencil(pDSV, CLEAR_DEPTH_FLAG, 1.f, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    // Get pretransform matrix that rotates the scene according the surface orientation
    auto SrfPreTransform = GetSurfacePretransformMatrix(float3{0, 0, 1});

    // Get projection matrix adjusted to the current screen orientation
    auto Proj = GetAdjustedProjectionMatrix(PI_F / 4.0f, 0.1f, 100.f);

    // Configuramos los viewports para las tres ventanas
    // Dividimos la pantalla en 3 partes horizontales
    const auto& SCDesc = m_pSwapChain->GetDesc();
    
    Viewport Viewports[3];
    // Ventana 1: Parte izquierda
    Viewports[0].TopLeftX = 0;
    Viewports[0].TopLeftY = 0;
    Viewports[0].Width = SCDesc.Width / 3;
    Viewports[0].Height = SCDesc.Height;
    Viewports[0].MinDepth = 0;
    Viewports[0].MaxDepth = 1;
    
    // Ventana 2: Parte central
    Viewports[1].TopLeftX = SCDesc.Width / 3;
    Viewports[1].TopLeftY = 0;
    Viewports[1].Width = SCDesc.Width / 3;
    Viewports[1].Height = SCDesc.Height;
    Viewports[1].MinDepth = 0;
    Viewports[1].MaxDepth = 1;
    
    // Ventana 3: Parte derecha
    Viewports[2].TopLeftX = 2 * SCDesc.Width / 3;
    Viewports[2].TopLeftY = 0;
    Viewports[2].Width = SCDesc.Width / 3;
    Viewports[2].Height = SCDesc.Height;
    Viewports[2].MinDepth = 0;
    Viewports[2].MaxDepth = 1;
    
    // Renderizamos la escena tres veces, una vez para cada viewport con su propia cámara
    for (int viewIdx = 0; viewIdx < 3; viewIdx++)
    {
        // Establecer el viewport actual
        m_pImmediateContext->SetViewports(1, &Viewports[viewIdx], SCDesc.Width, SCDesc.Height);
        
        // Seleccionar la matriz de vista correspondiente a este viewport
        float4x4 CurrentView;
        switch(viewIdx)
        {
            case 0: CurrentView = ViewWindow1; break; // Paneo y zoom
            case 1: CurrentView = ViewWindow2; break; // Control orbital
            case 2: CurrentView = ViewWindow3; break; // Cámara libre
        }
        
        // Calcular la matriz view-projection para este viewport
        float4x4 ViewProj = CurrentView * SrfPreTransform * Proj;
        
        // Actualizar los constantes del shader
        {
            MapHelper<float4x4> CBConstants(m_pImmediateContext, m_VSConstants, MAP_WRITE, MAP_FLAG_DISCARD);
            CBConstants[0] = ViewProj;
            CBConstants[1] = m_RotationMatrix;
        }
        
        // Actualizar la matriz de transformación del suelo para esta vista
        {
            MapHelper<float4x4> FloorTransform(m_pImmediateContext, m_FloorTransform, MAP_WRITE, MAP_FLAG_DISCARD);
            FloorTransform[0] = float4x4::Identity(); // Matriz de modelo
            FloorTransform[1] = ViewProj;             // Matriz de vista-proyección
        }

        // Renderizar primero el suelo
        {
            // Configurar los buffers de vértices e índices para el suelo
            const Uint64 offsets[] = {0};
            IBuffer*     pBuffs[]  = {m_FloorVertexBuffer};
            m_pImmediateContext->SetVertexBuffers(0, _countof(pBuffs), pBuffs, offsets, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, SET_VERTEX_BUFFERS_FLAG_RESET);
            m_pImmediateContext->SetIndexBuffer(m_FloorIndexBuffer, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            
            // Configurar el pipeline state y recursos del shader
            m_pImmediateContext->SetPipelineState(m_pFloorPSO);
            m_pImmediateContext->CommitShaderResources(m_FloorSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            
            // Dibujar el suelo
            DrawIndexedAttribs DrawAttrs;
            DrawAttrs.IndexType = VT_UINT32;
            DrawAttrs.NumIndices = 6; // Dos triángulos (6 índices)
            DrawAttrs.Flags = DRAW_FLAG_VERIFY_ALL;
            m_pImmediateContext->DrawIndexed(DrawAttrs);
        }

        // Luego renderizar el móvil
        {
            // Configurar los buffers de vértices e índices para el móvil
            const Uint64 offsets[] = {0, 0};
            IBuffer*     pBuffs[]  = {m_CubeVertexBuffer, m_InstanceBuffer};
            m_pImmediateContext->SetVertexBuffers(0, _countof(pBuffs), pBuffs, offsets, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, SET_VERTEX_BUFFERS_FLAG_RESET);
            m_pImmediateContext->SetIndexBuffer(m_CubeIndexBuffer, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            
            // Configurar el pipeline state y recursos del shader
            m_pImmediateContext->SetPipelineState(m_pPSO);
            m_pImmediateContext->CommitShaderResources(m_SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            
            // Dibujar las instancias del móvil
            DrawIndexedAttribs DrawAttrs;
            DrawAttrs.IndexType = VT_UINT32;
            DrawAttrs.NumIndices = 36;
            DrawAttrs.NumInstances = 24; // Número de instancias del móvil
            DrawAttrs.Flags = DRAW_FLAG_VERIFY_ALL;
            m_pImmediateContext->DrawIndexed(DrawAttrs);
        }
    }
}

} // namespace Diligent
