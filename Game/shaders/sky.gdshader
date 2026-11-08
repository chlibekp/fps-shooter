// Procedural sky: atmosphere gradient, sun disc and glow, and drifting
// fbm clouds lit from the sun side. No textures.
shader_type sky;

uniform vec3 zenith : source_color = vec3(0.20, 0.38, 0.66);
uniform vec3 horizon : source_color = vec3(0.70, 0.76, 0.82);
uniform vec3 ground : source_color = vec3(0.32, 0.30, 0.28);
uniform float cloud_cover : hint_range(0.0, 1.0) = 0.45;
uniform float cloud_speed = 0.004;

float hash12(vec2 p) {
	vec3 p3 = fract(vec3(p.xyx) * 0.1031);
	p3 += dot(p3, p3.yzx + 33.33);
	return fract((p3.x + p3.y) * p3.z);
}

float vnoise(vec2 p) {
	vec2 i = floor(p);
	vec2 f = fract(p);
	f = f * f * (3.0 - 2.0 * f);
	return mix(mix(hash12(i), hash12(i + vec2(1, 0)), f.x), mix(hash12(i + vec2(0, 1)), hash12(i + vec2(1, 1)), f.x), f.y);
}

float fbm(vec2 p) {
	float s = 0.0;
	float a = 0.5;
	for (int i = 0; i < 6; i++) {
		s += a * vnoise(p);
		p = p * 2.02 + vec2(3.1, 7.7);
		a *= 0.5;
	}
	return s;
}

void sky() {
	vec3 d = EYEDIR;
	vec3 sun_dir = LIGHT0_DIRECTION;
	vec3 sun_col = LIGHT0_COLOR * LIGHT0_ENERGY;
	float h = d.y;
	vec3 col = mix(horizon, zenith, pow(clamp(h, 0.0, 1.0), 0.45));
	col = mix(col, ground, smoothstep(0.0, -0.08, h));
	float sd = max(dot(d, sun_dir), 0.0);
	col += sun_col * (pow(sd, 8.0) * 0.08 + pow(sd, 400.0) * 0.6);
	if (sd > 0.9995) {
		col += sun_col * 8.0;
	}
	if (h > 0.0) {
		// Project onto a cloud plane; fade toward the horizon.
		vec2 uv = d.xz / (h + 0.08) * 1.6 + vec2(TIME * cloud_speed, TIME * cloud_speed * 0.4);
		float n = fbm(uv);
		float c = smoothstep(1.0 - cloud_cover, 1.0 - cloud_cover + 0.35, n);
		float lit = fbm(uv + sun_dir.xz * 0.06);
		vec3 cloud = mix(vec3(0.62, 0.64, 0.68), vec3(1.0, 0.97, 0.92), clamp(1.0 - (lit - n) * 4.0, 0.0, 1.0));
		cloud += sun_col * pow(sd, 6.0) * 0.25;
		col = mix(col, cloud, c * smoothstep(0.0, 0.25, h) * 0.9);
	}
	COLOR = col;
}
