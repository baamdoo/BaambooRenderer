#ifndef _HLSL_MATERIAL_TEXTURES_HEADER
#define _HLSL_MATERIAL_TEXTURES_HEADER

bool FindMaterialTexture(
    MaterialData material,
    uint semantic,
    out MaterialTextureData binding)
{
    binding = (MaterialTextureData)0;

    if (material.textureCount == 0u)
        return false;

    StructuredBuffer< MaterialTextureData > MaterialTextures =
        GetResource(g_MaterialTextures.index);

    [loop]
    for (uint bindingIndex = 0u; bindingIndex < material.textureCount; ++bindingIndex)
    {
        MaterialTextureData candidate =
            MaterialTextures[material.textureOffset + bindingIndex];

        if (candidate.semantic == semantic && candidate.textureID != INVALID_INDEX)
        {
            binding = candidate;
            return true;
        }
    }

    return false;
}

float SelectMaterialTextureScalar(float4 value, uint channel)
{
    if (channel == MATERIAL_TEXTURE_CHANNEL_G)
        return value.g;
    if (channel == MATERIAL_TEXTURE_CHANNEL_B)
        return value.b;
    if (channel == MATERIAL_TEXTURE_CHANNEL_A)
        return value.a;
    return value.r;
}

float4 DecodeMaterialTextureSample(float4 value, uint channel)
{
    if (channel == MATERIAL_TEXTURE_CHANNEL_RGB ||
        channel == MATERIAL_TEXTURE_CHANNEL_RGBA)
    {
        return value;
    }

    return SelectMaterialTextureScalar(value, channel).xxxx;
}

bool SampleMaterialTextureLevel(
    MaterialData material,
    uint semantic,
    float2 uv,
    out float4 value,
    out uint channel)
{
    MaterialTextureData binding;
    if (!FindMaterialTexture(material, semantic, binding))
    {
        value = 0.0;
        channel = MATERIAL_TEXTURE_CHANNEL_R;
        return false;
    }

    Texture2D textureMap = GetResource(binding.textureID);
    value = DecodeMaterialTextureSample(
        textureMap.SampleLevel(g_TrilinearWrapSampler, uv, 0), binding.channel);
    channel = binding.channel;
    return true;
}

bool SampleMaterialTextureGrad(
    MaterialData material,
    uint semantic,
    float2 uv,
    float2 ddxUV,
    float2 ddyUV,
    out float4 value,
    out uint channel)
{
    MaterialTextureData binding;
    if (!FindMaterialTexture(material, semantic, binding))
    {
        value = 0.0;
        channel = MATERIAL_TEXTURE_CHANNEL_R;
        return false;
    }

    Texture2D textureMap = GetResource(binding.textureID);
    value = DecodeMaterialTextureSample(
        textureMap.SampleGrad(g_AnisotropicWrapSampler, uv, ddxUV, ddyUV), binding.channel);
    channel = binding.channel;
    return true;
}

#endif // _HLSL_MATERIAL_TEXTURES_HEADER