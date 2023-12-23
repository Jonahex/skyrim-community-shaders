static float GammaCorrectionValue = 2.2;

float RGBToLuminance(float3 color)
{
	return dot(color, float3(0.2125, 0.7154, 0.0721));
}

float RGBToLuminanceAlternative(float3 color)
{
	return dot(color, float3(0.3, 0.59, 0.11));
}

float RGBToLuminance2(float3 color)
{
	return dot(color, float3(0.299, 0.587, 0.114));
}

float3 sRGB2Lin(float3 color)
{
	return color > 0.04045 ? pow(color / 1.055 + 0.055 / 1.055, 2.4) : color / 12.92;
}

float3 Lin2sRGB(float3 color)
{
	return color > 0.0031308 ? 1.055 * pow(color, 1.0 / 2.4) - 0.055 : 12.92 * color;
}

float3 HUE2Lin(float h)
{
	float r = abs(h * 6 - 3) - 1;
	float g = 2 - abs(h * 6 - 2);
	float b = 2 - abs(h * 6 - 4);
	return saturate(float3(r, g, b));
}

float3 HSV2Lin(float3 hsv)
{
	float3 rgb = HUE2Lin(hsv.x);
	return ((rgb - 1) * hsv.y + 1) * hsv.z;
}

float3 Lin2HCV(float3 color)
{
	float4 p = (color.g < color.b)	? float4(color.bg, -1.0f, 2.0f / 3.0f): float4(color.gb, 0.0f, -1.0f / 3.0f);
	float4 q = (color.r < p.x)	? float4(p.xyw, color.r)				: float4(color.r, p.yzx);
	float chroma = q.x - min(q.w, q.y);
	float hue = abs((q.w - q.y) / (6.0f * chroma + 1e-10f) + q.z);
	return float3(hue, chroma, q.x);
}

float3 Lin2HSV(float3 color)
{
	float3 hcv = Lin2HCV(color);
	float s = hcv.y / (hcv.z + 1e-10f);
	return float3(hcv.x, s, hcv.z);
}