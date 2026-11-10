// Procedural world-space surfaces for the map geometry: no texture files,
// every pattern is generated here (original art). Boxes are axis-aligned, so
// each face picks the projection plane of its normal; patterns stay
// continuous across neighbouring boxes and destruction cells.
shader_type spatial;
render_mode blend_mix, cull_back, depth_draw_opaque;

// 0 concrete, 1 brick, 2 wood, 3 drywall, 4 painted metal, 5 (glass: own material),
// 6 reinforced plate, 7 ground
uniform int pattern = 0;
uniform vec3 color_a : source_color = vec3(0.55, 0.55, 0.53);
uniform vec3 color_b : source_color = vec3(0.42, 0.42, 0.40);
uniform float roughness_base : hint_range(0.0, 1.0) = 0.85;
uniform float metallic_base : hint_range(0.0, 1.0) = 0.0;
uniform float bump_strength = 1.0;
// Per object: paint colour (containers, crates) and a random seed.
instance uniform vec3 tint : source_color = vec3(1.0);
instance uniform float seed = 0.0;

varying vec3 wpos;
varying vec3 wnorm;

void vertex() {
	wpos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	wnorm = normalize((MODEL_MATRIX * vec4(NORMAL, 0.0)).xyz);
}

float hash12(vec2 p) {
	vec3 p3 = fract(vec3(p.xyx) * 0.1031);
	p3 += dot(p3, p3.yzx + 33.33);
	return fract((p3.x + p3.y) * p3.z);
}

float vnoise(vec2 p) {
	vec2 i = floor(p);
	vec2 f = fract(p);
	f = f * f * (3.0 - 2.0 * f);
	float a = hash12(i);
	float b = hash12(i + vec2(1.0, 0.0));
	float c = hash12(i + vec2(0.0, 1.0));
	float d = hash12(i + vec2(1.0, 1.0));
	return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float fbm(vec2 p) {
	float s = 0.0;
	float a = 0.5;
	for (int i = 0; i < 5; i++) {
		s += a * vnoise(p);
		p = p * 2.03 + vec2(17.1, 9.7);
		a *= 0.5;
	}
	return s;
}

// x: colour blend (0 = color_a, 1 = color_b), y: height, z: roughness offset, w: cavity (0..1, 1 = recessed)
vec4 surface(vec2 uv, int pat, float detail) {
	if (pat == 1) { // running-bond brick, 24 x 7.5 cm with 1 cm mortar
		vec2 b = vec2(0.24, 0.075);
		float row = floor(uv.y / b.y);
		float x = uv.x / b.x + (mod(row, 2.0) > 0.5 ? 0.5 : 0.0);
		vec2 id = vec2(floor(x), row);
		vec2 f = vec2(fract(x) * b.x, fract(uv.y / b.y) * b.y);
		float edge = min(min(f.x, b.x - f.x), min(f.y, b.y - f.y));
		float mortar = (1.0 - smoothstep(0.004, 0.009, edge)) * detail;
		float brick_var = hash12(id + seed);
		float speck = mix(0.5, fbm(uv * 40.0), detail);
		// Bricks use the dark end (0..0.3), mortar the light end (1); far away both blend to their average.
		float brick_c = mix(0.15, brick_var * 0.25 + speck * 0.1, detail);
		float c = mix(mix(brick_c, 0.2, 1.0 - detail), 1.0, mortar);
		float h = (1.0 - mortar) * (0.8 + 0.2 * speck);
		return vec4(c, h, mortar * 0.1, mortar);
	}
	if (pat == 2) { // wood planks along u, 14 cm wide
		float w = 0.14;
		float plank = floor(uv.y / w);
		float along = uv.x + hash12(vec2(plank, seed)) * 7.0;
		float gap = 1.0 - smoothstep(0.002, 0.005, min(fract(uv.y / w) * w, w - fract(uv.y / w) * w));
		float endj = 1.0 - smoothstep(0.002, 0.005, abs(fract(along / 2.4) - 0.5) * 2.4 - 1.195);
		float grain = fbm(vec2(along * 0.6, uv.y * 40.0)) * 0.7 + 0.3 * sin(along * 3.0 + fbm(vec2(along, uv.y * 8.0)) * 6.0);
		float c = clamp(hash12(vec2(plank, 3.0 + seed)) * 0.5 + grain * 0.5, 0.0, 1.0);
		float cav = max(gap, endj);
		return vec4(mix(c, 1.0, cav * 0.6), (1.0 - cav) * (0.9 + 0.1 * grain), 0.0, cav);
	}
	if (pat == 3) { // painted plaster / drywall
		float n = fbm(uv * 3.0);
		float fine = vnoise(uv * 180.0);
		return vec4(n * 0.35, fine * 0.15, 0.0, 0.0);
	}
	if (pat == 4) { // painted corrugated metal with wear and rust
		float rib = mix(0.5, 0.5 + 0.5 * sin(uv.x * 6.2831 / 0.15), detail);
		float wear = smoothstep(0.76, 0.84, fbm(uv * 3.0 + seed * 3.1)) * 0.6;
		// Rust streaks run down from seams rather than forming blobs.
		float streak = fbm(vec2(uv.x * 9.0, uv.y * 0.8) + seed);
		float rust = smoothstep(0.66, 0.82, streak) * smoothstep(0.35, 0.75, fbm(uv * 0.9 + 11.0 + seed)) * 0.8;
		return vec4(clamp(wear * 0.6 + rust, 0.0, 1.0), rib, rust * 0.3 - wear * 0.3, 0.0);
	}
	if (pat == 6) { // studded steel plate with a 0.6 m bolt grid
		vec2 g = fract(uv / 0.6) - 0.5;
		float bolt = 1.0 - smoothstep(0.015, 0.025, length(g * 0.6));
		vec2 s = fract(uv * vec2(25.0, 25.0)) - 0.5;
		float studs = smoothstep(0.25, 0.15, abs(s.x + s.y * 0.6) + abs(s.y - s.x * 0.6) * 0.5);
		float n = fbm(uv * 4.0);
		return vec4(n * 0.5, studs * 0.4 + bolt, -0.1, 0.0);
	}
	if (pat == 7) { // yard ground
		// Concrete slabs (6 m, sealed joints) with patches of asphalt; hairline
		// cracks, oil stains, tyre tracks, grit and puddles.
		float region = fbm(uv * 0.022 + seed * 0.1);
		float asphalt = smoothstep(0.5, 0.56, region);
		vec2 cell = floor(uv / 6.0);
		vec2 f = abs(fract(uv / 6.0) - 0.5) * 6.0;
		float joint = (1.0 - smoothstep(0.008, 0.02, 3.0 - max(f.x, f.y))) * mix(0.4, 1.0, detail) * (1.0 - asphalt);
		float slab = hash12(cell + seed) * 0.22;
		float big = fbm(uv * 0.08);
		float mid = fbm(uv * 0.9);
		// Hairline cracks: thin ridges of a warped noise.
		float cn = fbm(uv * 0.45 + vec2(fbm(uv * 0.9), fbm(uv * 0.9 + 3.1)) * 0.6);
		float crack = (1.0 - smoothstep(0.0012, 0.0035, abs(cn - 0.5))) * smoothstep(0.55, 0.68, fbm(uv * 0.2 + 7.0)) * detail;
		float oil = smoothstep(0.64, 0.8, fbm(uv * 0.35 + 5.0)) * 0.7;
		float tracks = asphalt * smoothstep(0.55, 0.75, fbm(vec2(uv.x * 0.08, uv.y * 1.6) + 2.0)) * 0.5;
		float grit = vnoise(uv * 70.0) * detail;
		float aggregate = smoothstep(0.6, 0.9, vnoise(uv * 160.0)) * detail;
		float concrete_c = clamp(slab + big * 0.3 + mid * 0.22 - oil * 0.55 + joint * 0.8 + crack * 0.45, 0.0, 1.0);
		float asphalt_c = clamp(0.82 + mid * 0.12 - aggregate * 0.25 + tracks * 0.15 - oil * 0.1, 0.0, 1.0);
		float c = mix(concrete_c, asphalt_c, asphalt);
		// Puddles in low spots: standing water (a mirror, keeps the ground
		// colour under it) inside a damp, darker, semi-glossy margin.
		float low = fbm(uv * 0.11 + 13.0) + (grit - 0.5) * 0.02;
		float damp = smoothstep(0.66, 0.695, low);
		float puddle = smoothstep(0.695, 0.71, low) * (0.6 + 0.4 * asphalt);
		float h = mix(mid * 0.2 + grit * 0.15 - joint * 0.5 - crack * 0.6, aggregate * 0.25 + grit * 0.1, asphalt) * (1.0 - puddle);
		float rough = -oil * 0.3 - damp * 0.2 - puddle * 0.68;
		return vec4(clamp(c + damp * 0.06, 0.0, 1.0), h, rough, max(max(joint, crack), oil * 0.3));
	}
	// concrete: mottled, with stains and fine pores; formwork seams every 1.2 m
	float mott = fbm(uv * 1.2);
	float stain = smoothstep(0.45, 0.8, fbm(uv * 0.15 + seed));
	float pores = smoothstep(0.75, 0.9, vnoise(uv * 90.0)) * detail;
	vec2 sv = abs(fract(uv / 1.2) - 0.5) * 1.2;
	float seam = (1.0 - smoothstep(0.003, 0.006, 0.6 - max(sv.x, sv.y))) * detail;
	return vec4(clamp(mott * 0.6 + stain * 0.5, 0.0, 1.0), 1.0 - pores * 0.3 - seam * 0.5, pores * 0.1, max(pores * 0.5, seam));
}

void fragment() {
	vec3 n = normalize(wnorm);
	vec3 an = abs(n);
	// Plane of projection: u along the face, v up the wall (or along Z on floors).
	vec2 uv;
	vec3 tu;
	vec3 tv;
	if (an.y >= an.x && an.y >= an.z) {
		uv = wpos.xz;
		tu = vec3(1.0, 0.0, 0.0);
		tv = vec3(0.0, 0.0, 1.0);
	} else if (an.x >= an.z) {
		uv = vec2(wpos.z, wpos.y);
		tu = vec3(0.0, 0.0, 1.0);
		tv = vec3(0.0, 1.0, 0.0);
	} else {
		uv = vec2(wpos.x, wpos.y);
		tu = vec3(1.0, 0.0, 0.0);
		tv = vec3(0.0, 1.0, 0.0);
	}
	// Fine detail fades out where a pixel covers more than ~1 cm (no moire).
	float px = length(fwidth(uv));
	float detail = 1.0 - smoothstep(0.004, 0.02, px);
	vec4 s = surface(uv, pattern, detail);
	// Bump from the height field.
	float e = 0.004;
	float hx = surface(uv + vec2(e, 0.0), pattern, detail).y;
	float hy = surface(uv + vec2(0.0, e), pattern, detail).y;
	vec2 grad = vec2(hx - s.y, hy - s.y) / e * 0.004 * bump_strength;
	vec3 wn = normalize(n - tu * grad.x - tv * grad.y);
	NORMAL = normalize((VIEW_MATRIX * vec4(wn, 0.0)).xyz);

	vec3 col = mix(color_a, color_b, s.x) * tint;
	float metal = metallic_base;
	if (pattern == 4) {
		// Paint is not metallic; worn spots show bare steel, rust is dark brown.
		float rust = clamp(s.z * 3.33, 0.0, 1.0);
		float bare = clamp(-s.z * 3.33, 0.0, 1.0);
		col = mix(col, vec3(0.28, 0.28, 0.29), bare);
		col = mix(col, vec3(0.30, 0.16, 0.09), rust);
		metal = mix(0.05, 0.85, bare) * (1.0 - rust);
	}
	// Grime collects low on walls (within half a metre of the floor level
	// below), and rain leaves vertical streaks on outer walls.
	float grime = (1.0 - smoothstep(0.0, 0.6, fract(wpos.y / 3.5) * 3.5)) * (1.0 - an.y) * 0.25;
	float streaks = smoothstep(0.5, 0.75, fbm(vec2(uv.x * 2.2, wpos.y * 0.25) + seed)) * (1.0 - an.y) * 0.18;
	col *= (1.0 - grime) * (1.0 - streaks);
	ALBEDO = col;
	ROUGHNESS = clamp(roughness_base + s.z, 0.05, 1.0);
	METALLIC = metal;
	AO = 1.0 - s.w * 0.6;
	AO_LIGHT_AFFECT = 0.3;
}
