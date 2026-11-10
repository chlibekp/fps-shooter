// Distant scenery: building facades with storeys and window grids (dark
// glass, some lit) from world position, flat roofs; or foliage with a
// leafy normal. Colour per instance (multimesh colours).
shader_type spatial;
render_mode blend_mix, cull_back;

uniform bool foliage = false;

varying vec3 wpos;
varying vec3 wnorm;
varying vec3 col;

void vertex() {
	wpos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	wnorm = normalize((MODEL_MATRIX * vec4(NORMAL, 0.0)).xyz);
	col = COLOR.rgb;
}

float hash12(vec2 p) {
	vec3 p3 = fract(vec3(p.xyx) * 0.1031);
	p3 += dot(p3, p3.yzx + 33.33);
	return fract((p3.x + p3.y) * p3.z);
}

float vnoise(vec3 p) {
	vec3 i = floor(p);
	vec3 f = fract(p);
	f = f * f * (3.0 - 2.0 * f);
	float n = 0.0;
	float a = mix(mix(hash12(i.xy + i.z * 17.0), hash12(i.xy + vec2(1, 0) + i.z * 17.0), f.x),
		mix(hash12(i.xy + vec2(0, 1) + i.z * 17.0), hash12(i.xy + vec2(1, 1) + i.z * 17.0), f.x), f.y);
	float b = mix(mix(hash12(i.xy + (i.z + 1.0) * 17.0), hash12(i.xy + vec2(1, 0) + (i.z + 1.0) * 17.0), f.x),
		mix(hash12(i.xy + vec2(0, 1) + (i.z + 1.0) * 17.0), hash12(i.xy + vec2(1, 1) + (i.z + 1.0) * 17.0), f.x), f.y);
	return mix(a, b, f.z);
}

void fragment() {
	vec3 n = normalize(wnorm);
	if (foliage) {
		float l = vnoise(wpos * 1.6) * 0.6 + vnoise(wpos * 4.0) * 0.4;
		vec3 g = vec3(vnoise(wpos * 2.0 + 3.0), vnoise(wpos * 2.0 + 7.0), vnoise(wpos * 2.0 + 11.0)) - 0.5;
		NORMAL = normalize((VIEW_MATRIX * vec4(normalize(n + g * 1.4), 0.0)).xyz);
		ALBEDO = col * (0.6 + 0.6 * l);
		ROUGHNESS = 1.0;
	} else {
	vec3 c = col;
	float rough = 0.85;
	if (abs(n.y) < 0.5) {
		float u = abs(n.x) > abs(n.z) ? wpos.z : wpos.x;
		float floor_h = 3.4;
		float storey = floor(wpos.y / floor_h);
		vec2 cell = vec2(floor(u / 3.0), storey);
		vec2 f = vec2(fract(u / 3.0), fract(wpos.y / floor_h));
		float win = step(0.18, f.x) * step(f.x, 0.82) * step(0.3, f.y) * step(f.y, 0.82);
		float lit = step(0.9, hash12(cell + floor(wpos.xz / 40.0)));
		vec3 glass = mix(vec3(0.05, 0.06, 0.07), vec3(0.6, 0.55, 0.42), lit * 0.5);
		c = mix(c, glass, win * step(1.0, wpos.y));
		rough = mix(0.85, 0.15, win);
		// Floor bands.
		c *= 1.0 - (1.0 - smoothstep(0.0, 0.06, f.y)) * 0.25;
	} else {
		c *= 0.7;
	}
	ALBEDO = c;
	ROUGHNESS = rough;
	}
}
