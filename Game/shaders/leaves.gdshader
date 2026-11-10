// Close-range foliage (hedges, shrubs): a lumpy, displaced leaf mass with
// procedural leaf clusters, dark occluded gaps between them, bump from the
// leaf relief and light through the leaves. A box mesh can be rounded into a
// trimmed hedge (instance half_size > 0; the mesh must not be scaled).
shader_type spatial;
render_mode cull_back;

uniform vec3 leaf_color : source_color = vec3(0.1, 0.17, 0.06);
uniform float bulge = 0.05;          // lump displacement, metres
uniform float leaf_size = 0.035;     // leaf cluster size, metres
instance uniform vec3 half_size = vec3(0.0);   // rounded box: half extents (0 = keep the mesh)
instance uniform float corner = 0.25;           // rounded box: corner radius
instance uniform vec3 tint : source_color = vec3(1.0);

varying vec3 wpos;
varying vec3 vcol;

float hash(vec3 p) {
	p = fract(p * 0.3183099 + vec3(0.1, 0.2, 0.3));
	p *= 17.0;
	return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float noise(vec3 x) {
	vec3 i = floor(x);
	vec3 f = fract(x);
	f = f * f * (3.0 - 2.0 * f);
	return mix(mix(mix(hash(i), hash(i + vec3(1, 0, 0)), f.x), mix(hash(i + vec3(0, 1, 0)), hash(i + vec3(1, 1, 0)), f.x), f.y),
		mix(mix(hash(i + vec3(0, 0, 1)), hash(i + vec3(1, 0, 1)), f.x), mix(hash(i + vec3(0, 1, 1)), hash(i + vec3(1, 1, 1)), f.x), f.y), f.z);
}

float fbm(vec3 p) {
	float a = 0.5;
	float s = 0.0;
	for (int i = 0; i < 4; i++) {
		s += a * noise(p);
		p *= 2.03;
		a *= 0.5;
	}
	return s;
}

// Distance to the nearest leaf centre (cellular noise), 0 at a leaf's middle.
float cells(vec3 p) {
	vec3 i = floor(p);
	vec3 f = fract(p);
	float d = 1.0;
	for (int x = -1; x <= 1; x++)
	for (int y = -1; y <= 1; y++)
	for (int z = -1; z <= 1; z++) {
		vec3 g = vec3(float(x), float(y), float(z));
		vec3 o = vec3(hash(i + g), hash(i + g + 11.1), hash(i + g + 23.7));
		d = min(d, length(g + o - f));
	}
	return d;
}

void vertex() {
	if (half_size.x > 0.0) {
		vec3 inner = clamp(VERTEX, -(half_size - corner), half_size - corner);
		vec3 n = VERTEX - inner;
		n = length(n) > 1e-5 ? normalize(n) : NORMAL;
		VERTEX = inner + n * corner;
		NORMAL = n;
	}
	vcol = COLOR.rgb;   // multimesh instance colour (white otherwise)
	wpos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	float lump = 0.035 / leaf_size;   // lumps scale with the leaves (trees: bigger)
	float n = fbm(wpos * 2.5 * lump) + 0.35 * fbm(wpos * 9.0 * lump);
	VERTEX += NORMAL * (n - 0.6) * bulge * 2.0;
}

void fragment() {
	float l = cells(wpos / leaf_size);
	float gap = smoothstep(0.5, 0.95, l);
	vec3 c = leaf_color * tint * vcol * (0.7 + 0.7 * fbm(wpos * 1.7 * 0.035 / leaf_size));
	c *= 0.85 + 0.3 * hash(floor(wpos / leaf_size));       // leaf to leaf variation
	c = mix(c * 1.2 + vec3(0.012, 0.02, 0.0), c * 0.22, gap);
	ALBEDO = c;
	ROUGHNESS = 0.7 + 0.2 * gap;
	SPECULAR = 0.35;
	AO = 1.0 - gap * 0.65;
	AO_LIGHT_AFFECT = 0.4;
	BACKLIGHT = c * 0.5;
	// Bump from the leaf relief (screen-space derivatives of the height).
	float h = ((1.0 - gap) * 0.7 + 0.3 * fbm(wpos * 2.1 / leaf_size)) * leaf_size * 0.35;
	vec3 dpdx = dFdx(VERTEX);
	vec3 dpdy = dFdy(VERTEX);
	vec3 r1 = cross(dpdy, NORMAL);
	vec3 r2 = cross(NORMAL, dpdx);
	float det = dot(dpdx, r1);
	vec3 grad = sign(det) * (dFdx(h) * r1 + dFdy(h) * r2);
	NORMAL = normalize(abs(det) * NORMAL - grad);
}
