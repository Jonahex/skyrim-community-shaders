// https://github.com/tgjones/slimshader-cpp/blob/master/src/Shaders/Sdk/Direct3D11/DetailTessellation11/POM.hlsl
// https://github.com/alandtse/SSEShaderTools/blob/main/shaders_vr/ParallaxEffect.h

// https://github.com/marselas/Zombie-Direct3D-Samples/blob/5f53dc2d6f7deb32eb2e5e438d6b6644430fe9ee/Direct3D/ParallaxOcclusionMapping/ParallaxOcclusionMapping.fx
// http://www.diva-portal.org/smash/get/diva2:831762/FULLTEXT01.pdf
// https://bartwronski.files.wordpress.com/2014/03/ac4_gdc.pdf
// https://www.artstation.com/blogs/andreariccardi/3VPo/a-new-approach-for-parallax-mapping-presenting-the-contact-refinement-parallax-mapping-technique

struct DisplacementParams
{
    float Scale;
    float Offset;
    float MinDisplacement;
    float MaxDisplacement;
};

float AdjustDisplacement(float displacement, DisplacementParams params)
{
    return clamp((displacement - 0.5) * params.Scale + 0.5 + params.Offset, params.MinDisplacement, params.MaxDisplacement);
}

float4 AdjustDisplacement(float4 displacement, DisplacementParams params)
{
    return float4(AdjustDisplacement(displacement.x, params), AdjustDisplacement(displacement.y, params), AdjustDisplacement(displacement.z, params), AdjustDisplacement(displacement.w, params));
}

float GetMipLevel(float2 coords, Texture2D<float4> tex)
{
	// Compute the current gradients:
	float2 textureDims;
	tex.GetDimensions(textureDims.x, textureDims.y);

	float2 texCoordsPerSize = coords * textureDims;

	float2 dxSize = ddx(texCoordsPerSize);
	float2 dySize = ddy(texCoordsPerSize);

	// Find min of change in u and v across quad: compute du and dv magnitude across quad
	float2 dTexCoords = dxSize * dxSize + dySize * dySize;

	// Standard mipmapping uses max here
	float minTexCoordDelta = max(dTexCoords.x, dTexCoords.y);

	// Compute the current mip level  (* 0.5 is effectively computing a square root before )
	float mipLevel = max(0.5 * log2(minTexCoordDelta), 0);

	mipLevel = (mipLevel) + 1;  // Adjust scaling
#if !defined(PARALLAX)
	mipLevel++;
#endif
	return mipLevel;
}

#if defined(LANDSCAPE)
float2 GetParallaxCoords(float distance, float2 coords, float mipLevel, float3 viewDir, float3x3 tbn, Texture2D<float4> tex, SamplerState texSampler, uint channel, float blend, DisplacementParams params, out float pixelOffset)
#else
float2 GetParallaxCoords(float distance, float2 coords, float mipLevel, float3 viewDir, float3x3 tbn, Texture2D<float4> tex, SamplerState texSampler, uint channel, DisplacementParams params, out float pixelOffset)
#endif
{
	pixelOffset = 0.5;

	float3 viewDirTS = normalize(mul(tbn, viewDir));
	distance /= (float)perPassParallax[0].MaxDistance;

	viewDirTS.z = ((viewDirTS.z * 0.5) + 0.5);
	viewDirTS.xy /= viewDirTS.z;

	float nearQuality = smoothstep(0.0, perPassParallax[0].CRPMRange, distance);
	float nearBlendToMid = smoothstep(perPassParallax[0].CRPMRange - perPassParallax[0].BlendRange, perPassParallax[0].CRPMRange, distance);
	float midBlendToFar = smoothstep(1.0 - perPassParallax[0].BlendRange, 1.0, distance);

	float maxHeight = perPassParallax[0].Height;
	float minHeight = maxHeight * 0.5;

	float2 output;
	if (nearBlendToMid < 1.0) {
		uint numSteps;
#if defined(PARALLAX)
		float quality = (1.0 - nearQuality);
		if (perPassParallax[0].EnableHighQuality) {
			numSteps = round(lerp(4, 64, quality));
			numSteps = clamp((numSteps + 3) & ~0x03, 4, 64);
		} else {
			numSteps = round(lerp(4, 32, quality));
			numSteps = clamp((numSteps + 3) & ~0x03, 4, 32);
		}
#else
#	if defined(LANDSCAPE)
		float quality = min(1.0 - nearQuality, pow(saturate(blend), 0.5));
		if (perPassParallax[0].EnableHighQuality) {
			numSteps = max(1, round(32 * quality));
			numSteps = clamp((numSteps + 3) & ~0x03, 4, 32);
		} else {
			numSteps = max(1, round(16 * quality));
			numSteps = clamp((numSteps + 3) & ~0x03, 4, 16);
		}
#	else
		if (perPassParallax[0].EnableHighQuality) {
			numSteps = max(1, round(32 * (1.0 - nearQuality)));
			numSteps = clamp((numSteps + 3) & ~0x03, 4, 32);
		} else {
			numSteps = max(1, round(16 * (1.0 - nearQuality)));
			numSteps = clamp((numSteps + 3) & ~0x03, 4, 16);
		}
#	endif
#endif
		float stepSize = 1.0 / ((float)numSteps + 1.0);

		float2 offsetPerStep = viewDirTS.xy * float2(maxHeight, maxHeight) * stepSize.xx;
		float2 prevOffset = viewDirTS.xy * float2(minHeight, minHeight) + coords.xy;

		float prevBound = 1.0;
		float prevHeight = 1.0;

		uint numStepsTemp = numSteps;

		float2 pt1 = 0;
		float2 pt2 = 0;

		bool contactRefinement = false;

		mipLevel--;

		[loop] while (numSteps > 0)
		{
			float4 currentOffset[2];
			currentOffset[0] = prevOffset.xyxy - float4(1, 1, 2, 2) * offsetPerStep.xyxy;
			currentOffset[1] = prevOffset.xyxy - float4(3, 3, 4, 4) * offsetPerStep.xyxy;
			float4 currentBound = prevBound.xxxx - float4(1, 2, 3, 4) * stepSize;

			float4 currHeight;
			currHeight.x = tex.SampleLevel(texSampler, currentOffset[0].xy, mipLevel)[channel];
			currHeight.y = tex.SampleLevel(texSampler, currentOffset[0].zw, mipLevel)[channel];
			currHeight.z = tex.SampleLevel(texSampler, currentOffset[1].xy, mipLevel)[channel];
			currHeight.w = tex.SampleLevel(texSampler, currentOffset[1].zw, mipLevel)[channel];
			
            currHeight = AdjustDisplacement(currHeight, params);

			bool4 testResult = currHeight >= currentBound;
			[branch] if (any(testResult))
			{
				float2 outOffset = 0;
				[flatten] if (testResult.w)
				{
					outOffset = currentOffset[1].xy;
					pt1 = float2(currentBound.w, currHeight.w);
					pt2 = float2(currentBound.z, currHeight.z);
				}
				[flatten] if (testResult.z)
				{
					outOffset = currentOffset[0].zw;
					pt1 = float2(currentBound.z, currHeight.z);
					pt2 = float2(currentBound.y, currHeight.y);
				}
				[flatten] if (testResult.y)
				{
					outOffset = currentOffset[0].xy;
					pt1 = float2(currentBound.y, currHeight.y);
					pt2 = float2(currentBound.x, currHeight.x);
				}
				[flatten] if (testResult.x)
				{
					outOffset = prevOffset;

					pt1 = float2(currentBound.x, currHeight.x);
					pt2 = float2(prevBound, prevHeight);
				}

				if (contactRefinement) {
					break;
				} else {
					contactRefinement = true;
					prevOffset = outOffset;
					prevBound = pt2.x;
					numSteps = numStepsTemp;
					stepSize /= (float)numSteps;
					offsetPerStep /= (float)numSteps;
					continue;
				}
			}

			prevOffset = currentOffset[1].zw;
			prevBound = currentBound.w;
			prevHeight = currHeight.w;
			numSteps -= 4;
		}

		float delta2 = pt2.x - pt2.y;
		float delta1 = pt1.x - pt1.y;

		float denominator = delta2 - delta1;

		float parallaxAmount = 0.0;
		if (denominator == 0.0) {
			parallaxAmount = 0.0;
		} else {
			parallaxAmount = (pt1.x * delta2 - pt2.x * delta1) / denominator;
		}

		if (perPassParallax[0].EnableHighQuality) {
			float delta2 = pt2.x - pt2.y;
			float delta1 = pt1.x - pt1.y;

			float denominator = delta2 - delta1;

			float parallaxAmount = 0.0;
			if (denominator == 0.0) {
				parallaxAmount = 0.0;
			} else {
				parallaxAmount = (pt1.x * delta2 - pt2.x * delta1) / denominator;
			}

			float offset = (1.0 - parallaxAmount) * -maxHeight + minHeight;
			pixelOffset = parallaxAmount;
			output = viewDirTS.xy * offset + coords.xy;
		} else {
			float offset = (1.0 - pt1.x) * -maxHeight + minHeight;
			pixelOffset = pt1.x;
			output = viewDirTS.xy * offset + coords.xy;
		}

		if (nearBlendToMid > 0.0) {
            float height = AdjustDisplacement(tex.Sample(texSampler, coords.xy)[channel], params);
			height = height * maxHeight - minHeight;
			pixelOffset = lerp(pt1.x, height, nearBlendToMid);
			output = lerp(output, viewDirTS.xy * height.xx + coords.xy, nearBlendToMid);
		}
	} else if (midBlendToFar < 1.0) {
        float height = AdjustDisplacement(tex.Sample(texSampler, coords.xy)[channel], params);
		if (midBlendToFar > 0.0) {
			maxHeight *= (1 - midBlendToFar);
			minHeight *= (1 - midBlendToFar);
		}
		pixelOffset = height;
		height = height * maxHeight - minHeight;
		output = viewDirTS.xy * height.xx + coords.xy;
	} else {
		output = coords;
	}

	return output;
}

// https://advances.realtimerendering.com/s2006/Tatarchuk-POM.pdf
// https://www.gamedevs.org/uploads/quadtree-displacement-mapping-with-height-blending.pdf

// Cheap method of creating soft shadows using height for a given light source
// Only uses 1 sample vs the 8 in the original paper's example, which makes this effect very scaleable at the cost of small details
float GetParallaxSoftShadowMultiplier(float2 coords, float mipLevel, float3 L, float sh0, Texture2D<float4> tex, SamplerState texSampler, uint channel, float quality, DisplacementParams params)
{
	if (quality > 0.0) {
		const float height = 0.025;
		const float2 rayDir = L.xy * height;

		const float h0 = 1.0 - sh0;
        const float h = 1.0 - AdjustDisplacement(tex.SampleLevel(texSampler, coords + rayDir, mipLevel)[channel], params);

		// Compare the difference between the two heights to see if the height blocks it
		const float occlusion = 1.0 - saturate((h0 - h) * 7.0);

		// Fade out with quality
		return lerp(1.0, occlusion, quality);
	}
	return 1.0;
}

#if defined(LANDSCAPE)
float GetParallaxSoftShadowMultiplierTerrain(PS_INPUT input, float2 coords[6], float mipLevel[6], float3 L, float sh0[6], float quality, DisplacementParams params[6])
{
	float occlusion = 0.0;

	if (input.LandBlendWeights1.x > 0)
		occlusion += GetParallaxSoftShadowMultiplier(coords[0], mipLevel[0], L, sh0[0], TexColorSampler, SampTerrainParallaxSampler, 3, quality, params[0]) * input.LandBlendWeights1.x;
	if (input.LandBlendWeights1.y > 0)
		occlusion += GetParallaxSoftShadowMultiplier(coords[1], mipLevel[1], L, sh0[1], TexLandColor2Sampler, SampTerrainParallaxSampler, 3, quality, params[1]) * input.LandBlendWeights1.y;
	if (input.LandBlendWeights1.z > 0)
		occlusion += GetParallaxSoftShadowMultiplier(coords[2], mipLevel[2], L, sh0[2], TexLandColor3Sampler, SampTerrainParallaxSampler, 3, quality, params[2]) * input.LandBlendWeights1.z;
	if (input.LandBlendWeights1.w > 0)
		occlusion += GetParallaxSoftShadowMultiplier(coords[3], mipLevel[3], L, sh0[3], TexLandColor4Sampler, SampTerrainParallaxSampler, 3, quality, params[3]) * input.LandBlendWeights1.w;
	if (input.LandBlendWeights2.x > 0)
		occlusion += GetParallaxSoftShadowMultiplier(coords[4], mipLevel[4], L, sh0[4], TexLandColor5Sampler, SampTerrainParallaxSampler, 3, quality, params[4]) * input.LandBlendWeights2.x;
	if (input.LandBlendWeights2.y > 0)
		occlusion += GetParallaxSoftShadowMultiplier(coords[5], mipLevel[5], L, sh0[5], TexLandColor6Sampler, SampTerrainParallaxSampler, 3, quality, params[5]) * input.LandBlendWeights2.y;
	return saturate(occlusion);  // Blend weights seem to go greater than 1.0 sometimes
}
#endif
