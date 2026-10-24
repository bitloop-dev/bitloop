#pragma once

#include <imgui.h>
#include <imgui_internal.h>
#include <bitloop/util/hashable.h>

#include <functional>
#include <string>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <stdio.h>
#include <cstdint>
#include <cstring>

enum ImSplineFlags_
{
	ImSplineFlags_None,
	ImSplineFlags_InvertY
};
typedef int ImSplineFlags;

enum struct SplineSerializationMode
{
	COMPRESS_BASE64,
	COMPRESS_SHORTEST,
	CPP_ARRAY
};

// Cubic polynomial segment data:
struct CubicSegment
{
	float x0, x1;  // domain of this segment
	// f(x) = a + b*(x - x0) + c*(x - x0)^2 + d*(x - x0)^3
	float a, b, c, d;
};

// Build a natural cubic spline from the given sorted x-values and function evalY.
inline std::vector<CubicSegment> computeNaturalCubicSpline(
	const std::vector<float>& xvals,
	std::function<float(float)> evalY)
{
	int n = (int)xvals.size() - 1; // number of segments
	if (n <= 0) {
		return {};
	}

	std::vector<float> a(n + 1), b(n), c(n + 1), d(n);
	std::vector<float> h(n);

	// Step 1: a[i] = y[i]
	for (int i = 0; i <= n; i++) {
		a[i] = evalY(xvals[i]);
	}
	// h[i] = x[i+1] - x[i]
	for (int i = 0; i < n; i++) {
		h[i] = xvals[i + 1] - xvals[i];
	}

	// Step 2: solve for c[i] via the tridiagonal system
	// For natural spline, c[0] = c[n] = 0
	std::vector<float> alpha(n + 1), l(n + 1), mu(n + 1), z(n + 1);

	// alpha[i] = 3/h[i]*(a[i+1]-a[i]) - 3/h[i-1]*(a[i]-a[i-1])
	for (int i = 1; i < n; i++) {
		alpha[i] = 3.0f * ((a[i + 1] - a[i]) / h[i]
			- (a[i] - a[i - 1]) / h[i - 1]);
	}

	// boundary conditions for natural spline
	alpha[0] = 0.0f;
	alpha[n] = 0.0f;
	c[0] = 0.0f;
	c[n] = 0.0f;

	// l, mu, z
	l[0] = 1.0f;
	mu[0] = 0.0f;
	z[0] = 0.0f;

	for (int i = 1; i < n; i++) {
		l[i] = 2.0f * (xvals[i + 1] - xvals[i - 1]) - h[i - 1] * mu[i - 1];
		mu[i] = h[i] / l[i];
		z[i] = (alpha[i] - h[i - 1] * z[i - 1]) / l[i];
	}
	l[n] = 1.0f;
	z[n] = 0.0f;
	c[n] = 0.0f;

	// back-substitution for c[i]
	for (int j = n - 1; j >= 0; j--) {
		c[j] = z[j] - mu[j] * c[j + 1];
	}

	// Step 3: compute b[i], d[i]
	std::vector<CubicSegment> segments(n);
	for (int i = 0; i < n; i++) {
		float hi = h[i];
		float temp = (c[i + 1] - c[i]) / (3.0f * hi);
		d[i] = temp;
		b[i] = ((a[i + 1] - a[i]) / hi) - (hi * (c[i + 1] + 2.0f * c[i]) / 3.0f);
	}

	// Build the output segments
	for (int i = 0; i < n; i++) {
		CubicSegment seg;
		seg.x0 = xvals[i];
		seg.x1 = xvals[i + 1];
		seg.a = a[i];
		seg.b = b[i];
		seg.c = c[i];
		seg.d = d[i];
		segments[i] = seg;
	}

	return segments;
}

inline float splineValue(const CubicSegment& seg, float x)
{
	float dx = x - seg.x0;
	return seg.a
		+ seg.b * dx
		+ seg.c * dx * dx
		+ seg.d * dx * dx * dx;
}

// --------------------------------------------------------------------------
// 1) Iterative Global Fitting with Error Bound
// --------------------------------------------------------------------------

// This function refines the knot list so that the final spline stays within
// 'tolerance' across the entire domain [xStart, xEnd], or until we hit
// maxKnots or a max iteration. The final x-values are returned sorted.
//
// Strategy:
//   - Begin with xvals = { xStart, xEnd }
//   - Repeatedly build the spline, measure error at multiple sample points
//   - Subdivide segments for points above tolerance
//   - Keep going until no improvements needed or we reach a limit
//   - This is a naive approach: each iteration might add multiple new knots.
inline std::vector<float> buildGlobalSplineWithErrorBound(
	float xStart, float xEnd,
	std::function<float(float)> evalY,
	float tolerance,
	int maxKnots,
	int maxIterations = 10,     // or some limit
	int testSamples = 20    // how many test points across domain
)
{
	// Start with 2 knots
	std::vector<float> xvals;
	xvals.push_back(xStart);
	xvals.push_back(xEnd);

	// We'll do up to maxIterations of refinement
	for (int iter = 0; iter < maxIterations; iter++)
	{
		// Build the spline from the current xvals
		std::vector<CubicSegment> spline = computeNaturalCubicSpline(xvals, evalY);

		// We test error at 'testSamples' points across [xStart, xEnd].
		// For each sample, we find which segment it belongs to and measure difference.
		float maxErrThisIteration = 0.0f;

		// We'll accumulate new subdivisions here:
		std::vector<float> newKnots;  // each x that needs to be inserted
		newKnots.reserve(testSamples);

		// Build a quick list of sample X's
		float domainLength = xEnd - xStart;
		float step = domainLength / (float)(testSamples - 1);

		for (int s = 0; s < testSamples; s++)
		{
			float xs = xStart + s * step;
			float ysTrue = evalY(xs);

			// Find which segment holds 'xs'. We'll do a binary search
			// or a simple linear scan since 'spline' is typically small.
			// But let's do a binary search for generality:
			int left = 0;
			int right = (int)spline.size() - 1;
			int segIndex = -1;

			while (left <= right) {
				int mid = (left + right) / 2;
				if (xs < spline[mid].x0)
					right = mid - 1;
				else if (xs > spline[mid].x1)
					left = mid + 1;
				else {
					segIndex = mid;
					break;
				}
			}
			if (segIndex < 0) {
				// If something odd happens (floating precision?), clamp to the ends:
				segIndex = (xs <= spline[0].x0) ? 0 : (int)spline.size() - 1;
			}

			// Evaluate the spline at xs
			float ysSpline = splineValue(spline[segIndex], xs);

			float err = std::fabs(ysTrue - ysSpline);
			if (err > maxErrThisIteration) {
				maxErrThisIteration = err;
			}

			// If error is above tolerance, we plan to subdivide
			if (err > tolerance)
			{
				newKnots.push_back(xs);
			}
		} // end for testSamples

		// If max error is within tolerance, we are done
		if (maxErrThisIteration <= tolerance) {
			break;
		}

		// Otherwise, we insert the new knots from this iteration
		// Sort them and merge them into xvals
		if (!newKnots.empty())
		{
			// Combine
			xvals.insert(xvals.end(), newKnots.begin(), newKnots.end());
			std::sort(xvals.begin(), xvals.end());

			// Remove duplicates or very close points if needed
			// to avoid floating precision issues:
			auto endIt = std::unique(xvals.begin(), xvals.end(), [](float a, float b) {
				return std::fabs(a - b) < 1e-7f;
			});
			xvals.erase(endIt, xvals.end());

			// If we exceed maxKnots, break
			if ((int)xvals.size() > maxKnots) {
				//std::cerr << "[Warning] Reached maxKnots=" << maxKnots
				//	<< " before achieving tolerance.\n";
				break;
			}
		}
		else {
			// no new knots? Then we can't improve. Break
			break;
		}
	} // end iteration loop

	return xvals;
}

inline float splineDerivative(const CubicSegment& segment, float x)
{
	float dx = x - segment.x0;
	// derivative = b + 2*c*(x - x0) + 3*d*(x - x0)^2
	return segment.b + 2.0f * segment.c * dx + 3.0f * segment.d * dx * dx;
}

inline void splineToBezierHandles(
	const std::vector<CubicSegment>& segments,
	ImVec2* outPoints,
	int maxPoints
)
{
	// We'll build (numSegments+1) anchors
	// each anchor has 3 entries: handle_in, anchor, handle_out
	int n = (int)segments.size();
	int needed = (n + 1) * 3;
	if (needed > maxPoints) {
		//std::cerr << "Warning: not enough space in outPoints.\n";
		return; // or handle partial
	}

	// Precompute derivative at each knot
	std::vector<float> slopes(n + 1);
	slopes[0] = splineDerivative(segments[0], segments[0].x0);
	for (int i = 1; i < n; i++) {
		// slope at interior knot i = derivative in segments[i-1] at x1
		// which equals derivative in segments[i] at x0 (C1 continuity).
		slopes[i] = splineDerivative(segments[i - 1], segments[i - 1].x1);
	}
	slopes[n] = splineDerivative(segments[n - 1], segments[n - 1].x1);

	int idx = 0;
	for (int i = 0; i < n; i++)
	{
		float xi, yi;
		if (i < n) {
			xi = segments[i].x0;
			yi = splineValue(segments[i], xi);
		}
		else {
			// last anchor uses x1 of the last segment
			xi = segments[n - 1].x1;
			yi = splineValue(segments[n - 1], xi);
		}
		ImVec2 anchor = { xi, yi };

		// default handle_in/out = anchor (avoid wild lines if we skip)
		ImVec2 handleIn = anchor;
		ImVec2 handleOut = anchor;

		// handle_in for anchor i (except i=0)
		if (i > 0) {
			float dx = segments[i - 1].x1 - segments[i - 1].x0;
			float slopeRight = slopes[i]; // slope at x_i
			float dx3 = dx / 3.0f;
			handleIn.x = anchor.x - dx3;
			handleIn.y = anchor.y - slopeRight * dx3;
		}

		// handle_out for anchor i (except i=n)
		if (i < n) {
			float dx = segments[i].x1 - segments[i].x0;
			float slopeLeft = slopes[i];
			float dx3 = dx / 3.0f;
			handleOut.x = anchor.x + dx3;
			handleOut.y = anchor.y + slopeLeft * dx3;
		}

		outPoints[idx + 0] = handleIn;
		outPoints[idx + 1] = anchor;
		outPoints[idx + 2] = handleOut;

		idx += 3;
	}
}

namespace ImSpline
{
	// Forward declare
	class Spline;
	bool BeginSplineEditor(const char* label, Spline* spline, ImRect* grid_r, float max_editor_size);
	void EndSplineEditor();

	// "Spline" object manages knots/handles (from external "points" float array)
	// generates spline "path", holds editor states, and provides efficient value 
	// lookup using:
	// > countIntersectsY (float x)
	// > intersectY		  (float x, int intersection_index)
	// > firstIntersectY  (float x)

	static const char* base64_chars =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
		"abcdefghijklmnopqrstuvwxyz"
		"0123456789+/";

	/*class SplineDataManager
	{
		float* buffer;
		int length;


	};*/

	constexpr size_t PointsArrSize(int points)
	{
		return points * 2;
	}

	constexpr size_t KnotsArrSize(int knots)
	{
		return knots * 6;
	}

	template <std::size_t N>
	constexpr std::size_t PointsArrSize(const float(&arr)[N]) {
		static_assert(N % 2 == 0, "Array size must be divisible by 2 for 2D points");
		return N / 2;
	}


	class Spline
	{
		enum OptimizationType
		{
			NONE,
			LINEAR,
			LINEAR_SCALED,
			LINEAR_INTERCEPT,
			LINEAR_SCALED_INTERCEPT,

		};

		// Editor states
		bool panning = false;
		ImVec2 pan_mouse_down_pos;        // Mouse position on middle click (drag)
		ImRect pan_mouse_down_vr;         // Viewport rect on middle click (drag)
		int dragging_index = -1;          // Point index of handle/knot being dragged
		ImVec2 h1_offset, h2_offset;      // Handle offsets (when dragging knot)
		float opposite_handle_dist = 0;   // Opposite knot handle dist when moving handle
		float xy_precision = 0.01f;

		bool bInitialized = false;

		// Externally provided x/y point buffer
		//float* points = nullptr;
		ImVector<ImVec2> point_arr;
		//int point_count = 0;
		//int point_count_max = 0;

		//inline ImVec2* pointVecArray() { return reinterpret_cast<ImVec2*>(points); }
		inline float* floatArray() const { return reinterpret_cast<float*>(point_arr.Data); }
		inline ImVec2* pointVecArray() { return point_arr.Data; }

		// Generated path info
		ImVector<ImVec2> path;
		int path_length = 0;
		ImRect path_bounds;
		float path_bounds_left = 0; // Store left-bound seperately for efficient index calculation

		// Generated spline path segment index lookup.
		// (used for quickly finding which segments might intersect at a given x-value)
		bool col_segments_dirty = true;
		int col_count = 0;
		float col_x_snap = 0;
		ImVector<ImVector<int>> col_segments;

		// Optimization
		OptimizationType optimization_type = OptimizationType::NONE;
		float linear_gradient = 0;
		float linear_intercept = 0;

		// Each time spline changes a hash value is generated. Useful for checking if 
		// spline has changed by comparing against previous value
		bl::hash_t spline_hash = 0;

		// Map x value to column index. First column at path left bound
		inline int intersectionColumn(float x) const
		{
			return static_cast<int>((x - path_bounds_left) / col_x_snap);
		}

		void onChanged()
		{
			generatePathSegments();
			buildPathSegmentMap();
			updateHash();
			checkForOptimizations();
		}

		ImVector<ImVec2> &generatePathSegments()
		{
			constexpr float inf = std::numeric_limits<float>().max();
			float x0 = inf, y0 = inf, x1 = -inf, y1 = -inf;
			float a_x, a_y, b_x, b_y, c_x, c_y, d_x, d_y, e_x, e_y;
			float p0_x, p0_y, p1_x, p1_y, p2_x, p2_y, p3_x, p3_y;
			float t = 0, t1 = 1;
			float px, py;
			float inc = 1.0f / static_cast<float>(path_length);

			path.clear();

			float* xy_points = floatArray();
			for (int i = 1; i < point_arr.Size - 4; i += 3)
			{
				p0_x = xy_points[i * 2];      p0_y = xy_points[i * 2 + 1];
				p1_x = xy_points[i * 2 + 2];  p1_y = xy_points[i * 2 + 3];
				p2_x = xy_points[i * 2 + 4];  p2_y = xy_points[i * 2 + 5];
				p3_x = xy_points[i * 2 + 6];  p3_y = xy_points[i * 2 + 7];

				t1 = (i == point_arr.Size - 5) ? (1 + inc) : 1;

				for (; t < t1; t += inc)
				{
					if (t > 1) t = 1;

					a_x = p0_x + ((p1_x - p0_x) * t);  a_y = p0_y + ((p1_y - p0_y) * t);
					b_x = p1_x + ((p2_x - p1_x) * t);  b_y = p1_y + ((p2_y - p1_y) * t);
					c_x = p2_x + ((p3_x - p2_x) * t);  c_y = p2_y + ((p3_y - p2_y) * t);

					d_x = a_x + ((b_x - a_x) * t);  d_y = a_y + ((b_y - a_y) * t);
					e_x = b_x + ((c_x - b_x) * t);  e_y = b_y + ((c_y - b_y) * t);

					px = d_x + ((e_x - d_x) * t);
					py = d_y + ((e_y - d_y) * t);

					if (px < x0) x0 = px;
					if (px > x1) x1 = px;
					if (py < y0) y0 = py;
					if (py > y1) y1 = py;

					path.push_back({ px, py });
				}

				t = fmod(t, 1.0f);
			}

			path_bounds = ImRect(x0, y0, x1, y1);
			path_bounds_left = x0;

			int columns_from_ratio = path_length / 1;           // target 1 segments per column
			float w = 10.0f;                                 // 10 world units per column
			int columns_from_width = (int)ceil((x1 - x0) / w);

			// Combine
			int best_guess = std::max(50, std::min(500, std::max(columns_from_ratio, columns_from_width)));
			col_count = best_guess;

			col_x_snap = path_bounds.GetWidth() / static_cast<float>(col_count);
			return path;
		}

		void buildPathSegmentMap()
		{
			if (col_x_snap < 1e-9)
				return;

			for (int i = 0; i < col_segments.Size; i++)
				col_segments[i].clear();

			col_segments.clear();
			col_segments.resize(col_count + 1, ImVector<int>());

			int i1 = path.size() - 1;
			for (int i = 0; i < i1; i++)
			{
				float p_min_x = path[i].x;
				float p_max_x = path[i + 1].x;
				if (p_min_x > p_max_x)
				{
					float tmp = p_min_x;
					p_min_x = p_max_x;
					p_max_x = tmp;
				}

				// Relative to left bound, which columns does this segment span?
				int x_min = intersectionColumn(p_min_x);
				int x_max = intersectionColumn(p_max_x);

				// It's possible this path segment spans across multiple map columns.
				// Add to all of them
				for (int x = x_min; x <= x_max; x++)
					col_segments[x].push_back(i);
			}

			col_segments_dirty = false;
		}

		void checkForOptimizations()
		{
			bool is_linear = getLinearGradientIntercept(linear_gradient, linear_intercept);
			if (is_linear)
			{
				if (fabsf(linear_intercept) < 1e-9)
				{
					// Crosses through origin
					if (fabsf(linear_gradient - 1.0f) < 1e-9)
						optimization_type = OptimizationType::LINEAR;
					else
						optimization_type = OptimizationType::LINEAR_SCALED;
				}
				else
				{
					if (fabsf(linear_gradient - 1.0f) < 1e-9)
						optimization_type = OptimizationType::LINEAR_INTERCEPT;
					else
						optimization_type = OptimizationType::LINEAR_SCALED_INTERCEPT;

				}
			}
			else
			{
				optimization_type = OptimizationType::NONE;
			}
		}

		// Hashing

		inline uint32_t floatBits(float f) {
			uint32_t u;
			std::memcpy(&u, &f, sizeof(float));
			return u;
		}

		bl::hash_t hashFloatArray(const float* data, std::size_t count) {
			bl::StableHasher hasher;
			for (std::size_t i = 0; i < count; ++i) {
				hasher.add(floatBits(data[i]));
			}
			return hasher.finish();
		}

		void updateHash()
		{
			spline_hash = hashFloatArray(floatArray(), point_arr.Size * 2);
		}

		// Serializing
		// 
		// Check if a character is a valid base64 character.
		inline bool is_base64(unsigned char c) {
			return (isalnum(c) || (c == '+') || (c == '/'));
		}

		static std::string base64_encode(const unsigned char* data, size_t len) {
			std::string encoded;
			encoded.reserve(((len + 2) / 3) * 4);

			size_t i = 0;
			while (i < len) {
				unsigned char a = data[i++];
				unsigned char b = (i < len ? data[i++] : 0);
				unsigned char c = (i < len ? data[i++] : 0);

				// Pack the three bytes into a 24-bit integer
				unsigned int triple = (a << 16) + (b << 8) + c;

				// Extract four 6-bit values and map them to base64 characters.
				encoded.push_back(base64_chars[(triple >> 18) & 0x3F]);
				encoded.push_back(base64_chars[(triple >> 12) & 0x3F]);
				encoded.push_back((i - 1 < len) ? base64_chars[(triple >> 6) & 0x3F] : '=');
				encoded.push_back((i < len) ? base64_chars[triple & 0x3F] : '=');
			}

			return encoded;
		}

		static unsigned int indexOf(char c) 
		{
			const char* pos = std::strchr(base64_chars, c);
			if (!pos)
				throw std::runtime_error("Invalid character in Base64 string");
			return static_cast<unsigned int>(pos - base64_chars);
		}

		static std::vector<unsigned char> base64_decode(const std::string& encoded)
		{
			size_t len = encoded.size();
			if (len % 4 != 0)
				throw std::runtime_error("Invalid Base64 string length");

			// Determine the number of padding characters.
			size_t padding = 0;
			if (len) {
				if (encoded[len - 1] == '=') padding++;
				if (encoded[len - 2] == '=') padding++;
			}

			std::vector<unsigned char> decoded;
			decoded.reserve((len / 4) * 3 - padding);

			// Process every 4 characters as a block.
			for (size_t i = 0; i < len; i += 4) {
				unsigned int sextet_a = (encoded[i] == '=') ? 0 : indexOf(encoded[i]);
				unsigned int sextet_b = (encoded[i + 1] == '=') ? 0 : indexOf(encoded[i + 1]);
				unsigned int sextet_c = (encoded[i + 2] == '=') ? 0 : indexOf(encoded[i + 2]);
				unsigned int sextet_d = (encoded[i + 3] == '=') ? 0 : indexOf(encoded[i + 3]);

				// Pack the 4 sextets into a 24-bit integer.
				unsigned int triple = (sextet_a << 18) | (sextet_b << 12) | (sextet_c << 6) | sextet_d;

				// Extract the original bytes from the 24-bit number.
				decoded.push_back((triple >> 16) & 0xFF);
				if (encoded[i + 2] != '=')
					decoded.push_back((triple >> 8) & 0xFF);
				if (encoded[i + 3] != '=')
					decoded.push_back(triple & 0xFF);
			}

			return decoded;
		}

		float distance(const ImVec2& a, const ImVec2& b) {
			float dx = a.x - b.x, dy = a.y - b.y;
			return std::sqrt(dx * dx + dy * dy);
		}

		ImVec2 normalize(const ImVec2& p) {
			float len = std::sqrt(p.x * p.x + p.y * p.y);
			return len > 0.0f ? ImVec2{ p.x / len, p.y / len } : ImVec2{ 0.0f, 0.0f };
		}

		inline float dot(const ImVec2& a, const ImVec2& b)
		{
			return a.x * b.x + a.y * b.y;
		}


	public:

		std::function<float(float)> equation;

		Spline() = default;
		Spline(const Spline& rhs)
		{
			*this = rhs;
		}
		Spline(int segment_count)
		{
			if (path_length != segment_count)
				bInitialized = false;
	
			if (bInitialized)
				return;

			point_arr.clear();
			path_length = segment_count;

			bInitialized = true;
			onChanged();
		}

		Spline(int segment_count, const char* serialized)
		{
			if (path_length != segment_count)
				bInitialized = false;

			if (bInitialized)
				return;

			path_length = segment_count;
			deserialize(serialized);

			bInitialized = true;
		}

		Spline(int segment_count, std::initializer_list<ImVec2> points)
		{
			create(segment_count, points);
		}


		~Spline()
		{
			for (int i = 0; i < col_segments.Size; i++)
				col_segments[i].clear();
			col_segments.clear();
		}

		void create(int segment_count, std::initializer_list<ImVec2> points)
		{
			if (path_length != segment_count)
				bInitialized = false;

			if (bInitialized)
				return;

			point_arr.clear();
			for (const ImVec2& p : points)
				point_arr.push_back(p);

			path_length = segment_count;

			bInitialized = true;
			onChanged();
		}

		// Assumes both lhs/rhs have preallocated point arrays
		Spline& operator =(const Spline& rhs)
		{
			// regular serialization not saving/loading path_length?
			//deserialize(rhs.serialize(SplineSerializationMode::CPP_ARRAY));

			panning = rhs.panning;
			pan_mouse_down_pos = rhs.pan_mouse_down_pos;        // Mouse position on middle click (drag)
			pan_mouse_down_vr = rhs.pan_mouse_down_vr;;         // Viewport rect on middle click (drag)
			dragging_index = rhs.dragging_index;          // Point index of handle/knot being dragged
			h1_offset = rhs.h1_offset;
			h2_offset = rhs.h2_offset;      // Handle offsets (when dragging knot)
			opposite_handle_dist = rhs.opposite_handle_dist;   // Opposite knot handle dist when moving handle
			xy_precision = rhs.xy_precision;      // 1e-6f;

			if (spline_hash != rhs.spline_hash)
			{
				bInitialized = rhs.bInitialized;
				point_arr = rhs.point_arr;

				path = rhs.path;
				path_length = rhs.path_length;
				path_bounds = rhs.path_bounds;
				path_bounds_left = rhs.path_bounds_left;

				col_segments_dirty = rhs.col_segments_dirty;
				col_count = rhs.col_count;
				col_x_snap = rhs.col_x_snap;

				// 2D deep clone
				col_segments.resize(rhs.col_segments.size(), ImVector<int>());
				for (int i = 0; i < rhs.col_segments.size(); i++)
					col_segments[i] = rhs.col_segments[i];

				optimization_type = rhs.optimization_type;
				linear_gradient = rhs.linear_gradient;
				linear_intercept = rhs.linear_intercept;

				spline_hash = rhs.spline_hash;
			}

			return *this;
		}

		bool operator ==(const Spline& rhs) const
		{
			if (dragging_index != rhs.dragging_index) return false;
			if (panning != rhs.panning) return false;
			return hash() == rhs.hash();
			///bool a_empty = point_arr.empty();
			///bool b_empty = rhs.point_arr.empty();
			///
			///if (a_empty && b_empty)
			///	return true; // Both empty = same
			///else if (a_empty || b_empty)
			///	return false; // Only one empty = different
			///
			///return memcmp(point_arr.Data, rhs.point_arr.Data, rhs.point_arr.size_in_bytes()) == 0;
		}
		bool operator !=(const Spline& rhs) const
		{
			return !(operator==(rhs));
		}
		
		ImVec2& operator[](int i)
		{
			return point_arr[i];
		}

		void copyFrom(const Spline& rhs)
		{
			operator=(rhs);
		}

		void fromEquation(
			float xStart,
			float xEnd,
			std::function<float(float)> evalY,
			float errorTolerance=0.001,
			int maxKnots=6,
			int maxIter = 10,
			int testSamples = 10
		)
		{
			equation = evalY;
			// Build a refined knot set with global error bound
			std::vector<float> xvals = buildGlobalSplineWithErrorBound(
				xStart, xEnd, evalY, errorTolerance, maxKnots, maxIter, testSamples
			);

			// Build the final spline
			std::vector<CubicSegment> segments = computeNaturalCubicSpline(xvals, evalY);

			// Convert that spline to (anchor, handle_in, handle_out) data
			ImVec2* points = pointVecArray();
			splineToBezierHandles(segments, points, point_arr.Size);

			onChanged();
		}

		/*void fromEquation(float xStart, float xEnd, std::function<float(float)> evalY)
		{
			int knot_count = point_count / 3;
			ImVector<ImVec2> knots;
			float step = (xEnd - xStart) / knot_count;

			// Sample knots
			for (int i = 0; i <= knot_count; ++i)
			{
				float x = xStart + i * step;
				knots.push_back({ x, evalY(x) });
			}

			ImVec2* points = pointVecArray();
			int point_index = 0;

			const float local_sample_offset = step * 0.1f;

			for (int i = 0; i < knot_count; ++i)
			{
				const ImVec2& p = knots[i];
				float x = p.x;

				// Sample slightly before and after this knot to estimate slope
				float dx = local_sample_offset;
				float xL = std::max(xStart, x - dx);
				float xR = std::min(xEnd, x + dx);

				ImVec2 left = { xL, evalY(xL) };
				ImVec2 right = { xR, evalY(xR) };

				// Estimate tangent from surrounding samples
				ImVec2 tangent = normalize(right - left);

				// Determine curvature scaling based on angle (optional refinement)
				float len = (i == 0) ? distance(p, knots[i + 1]) :
					(i == knot_count) ? distance(p, knots[i - 1]) :
					std::min(distance(p, knots[i - 1]), distance(p, knots[i + 1]));

				float handle_length = len / 3.0f;

				// Final handle positions
				ImVec2 handle_in = p - tangent * handle_length;
				ImVec2 handle_out = p + tangent * handle_length;

				points[point_index++] = handle_in;
				points[point_index++] = p;
				points[point_index++] = handle_out;
			}

			onChanged();
		}*/


		/*void fromEquation(float xStart, float xEnd, std::function<float(float)> evalY)
		{
			int knot_count = point_count / 3;

			ImVector<ImVec2> knots;
			float step = (xEnd - xStart) / knot_count;

			// Sample knots
			for (int i = 0; i <= knot_count; ++i)
			{
				float x = xStart + i * step;
				knots.push_back({ x, evalY(x) });
			}

			// Estimate handles using finite differences
			//spline.points.reserve((segments + 1) * 3); // One knot and two handles per segment
			ImVec2* points = pointVecArray();

			int point_index = 0;
			for (int i = 0; i < knot_count; ++i)
			{
				ImVec2 &p = knots[i];
				ImVec2 tangent;
				float scale;

				if (i == 0) {
					// Forward difference
					tangent = knots[i + 1] - p;
					scale = std::min(1.0f, distance(knots[i], knots[i + 1])) / 3.0f;
				}
				else if (i == knot_count) {
					// Backward difference
					tangent = p - knots[i - 1];
					scale = std::min(1.0f, distance(knots[i], knots[i - 1])) / 3.0f;
				}
				else {
					// Centered difference
					tangent = (knots[i + 1] - knots[i - 1]) / 2.0f;
					float prevLen = distance(knots[i], knots[i - 1]);
					float nextLen = distance(knots[i], knots[i + 1]);
					scale = std::min(prevLen, nextLen) / 3.0f;
				}

				ImVec2 unitTangent = normalize(tangent);
				ImVec2 handleOffset = unitTangent * scale;

				ImVec2 handleIn = p - handleOffset;
				ImVec2 handleOut = p + handleOffset;

				points[point_index++] = handleIn;  // Incoming handle
				points[point_index++] = p;         // First knot
				points[point_index++] = handleOut; // Outgoing handle
			}

			onChanged();
		}*/



		bool initialized() const
		{
			return bInitialized;
		}

		bool isKnot(int point_index) const
		{
			return ((point_index - 1) % 3) == 0;
		}

		bool isHandle(int point_index) const
		{
			return ((point_index - 1) % 3) != 0;
		}

		int countIntersectsY(float x) const
		{
			int counter = 0;

			// Check whether head/tail intersection is possible. Increase counters if valid
			float tail_knot_x = point_arr[1].x;// points[2];
			float tail_dx = point_arr[0].x - tail_knot_x;// points[0] - tail_knot_x;
			if ((tail_dx < 0 && x < tail_knot_x) || (tail_dx > 0 && x > tail_knot_x))
				counter++;
			
			float head_knot_x = point_arr[point_arr.Size-2].x;// points[(point_count - 2) * 2];
			float head_dx = point_arr[point_arr.Size-1].x - head_knot_x;// points[(point_count - 1) * 2] - head_knot_x;
			if ((head_dx < 0 && x < head_knot_x) || (head_dx > 0 && x > head_knot_x))
				counter++;

			// Is an intersection with spline possible? If not, return existing projection intersections
			int col = intersectionColumn(x);
			if (col < 0 || col > col_count)
				return counter;

			const ImVector<int>& segments = col_segments[col];
			for (int i = 0; i < segments.size(); i++)
			{
				int segment_i = segments[i];
				const ImVec2& p0 = path[segment_i];
				const ImVec2& p1 = path[segment_i + 1];
				float p_min_x = std::min(p0.x, p1.x);
				float p_max_x = std::max(p0.x, p1.x);

				float dx = p_max_x - p_min_x;
				if (dx > 1e-9 && x >= p_min_x && x < p_max_x)
					counter++;
			}

			return counter;
		}

		float intersectY(float x, int intersection_index = 0) const
		{
			//if ((IsMouseClicked(0)))
			//{
			//	int a = 5;
			//}

			switch (optimization_type)
			{
			case OptimizationType::LINEAR:					return x;
			case OptimizationType::LINEAR_INTERCEPT:		return x + linear_intercept;
			case OptimizationType::LINEAR_SCALED:			return x * linear_gradient;
			case OptimizationType::LINEAR_SCALED_INTERCEPT: return x * linear_gradient + linear_intercept;
			default: break;
			}

			int counter = 0;

			// Handle head/tail projections
			float tail_knot_x = point_arr[1].x;// points[2];
			float tail_dx = point_arr[0].x - tail_knot_x;// points[0] - tail_knot_x;

			if ((tail_dx < 0 && x < tail_knot_x) || (tail_dx > 0 && x > tail_knot_x))
			{
				// Intersection with tail projection possible. Only calculate if requested intersection_index
				float tail_knot_y = point_arr[1].y;// points[3];
				float tail_dy = point_arr[0].y - tail_knot_y;// points[1] - tail_knot_y;

				if (tail_dx * tail_dx > 1e-18f)
				{
					float t = (x - tail_knot_x) / tail_dx;
					if (t > 0.0f)
					{
						if (counter++ == intersection_index)
							return tail_knot_y + t * tail_dy;
					}
				}
			}

			float head_knot_x = point_arr[point_arr.Size - 2].x;
			float head_dx = point_arr[point_arr.Size-1].x - head_knot_x;// points[(point_count - 1) * 2] - head_knot_x;

			if ((head_dx < 0 && x < head_knot_x) || (head_dx > 0 && x > head_knot_x))
			{
				// Intersection with head projection possible. Only calculate if requested intersection_index
				float head_knot_y = point_arr[point_arr.Size - 2].y;
				float head_dy = point_arr[point_arr.Size - 1].y - head_knot_y;
				if (head_dx * head_dx > 1e-18f)
				{
					float t = (x - head_knot_x) / head_dx;
					if (t > 0.0f)
					{
						if (counter++ == intersection_index)
							return head_knot_y + t * head_dy;
					}
				}
			}

			int col = intersectionColumn(x);
			if (col >= 0 && col <= col_count)
			{
				const ImVector<int>& segments = col_segments[col];
				int segment_count = segments.size();

				for (int i = 0; i < segment_count; i++)
				{
					int segment_i = segments[i];
					const ImVec2& p0 = path[segment_i];
					const ImVec2& p1 = path[segment_i + 1];
					float p_min_x = std::min(p0.x, p1.x);
					float p_max_x = std::max(p0.x, p1.x);
					float dx = p_max_x - p_min_x;

					if (dx > 1e-9 && x >= p_min_x && x < p_max_x)
					{
						float t = (x - p0.x) / (p1.x - p0.x);
						if (counter++ == intersection_index)
							return p0.y + t * (p1.y - p0.y);
					}
				}
			}

			return std::numeric_limits<float>::quiet_NaN();
		}

		float firstIntersectY(float x) const
		{
			switch (optimization_type)
			{
			case OptimizationType::LINEAR:					return x;
			case OptimizationType::LINEAR_INTERCEPT:		return x + linear_intercept;
			case OptimizationType::LINEAR_SCALED:			return x * linear_gradient;
			case OptimizationType::LINEAR_SCALED_INTERCEPT: return x * linear_gradient + linear_intercept;
			default: break;
			}

			// Handle head/tail projections
			float tail_knot_x = point_arr[1].x;// points[2];
			float tail_dx = point_arr[0].x - tail_knot_x; //points[0] - tail_knot_x;

			if ((tail_dx < 0 && x < tail_knot_x) || (tail_dx > 0 && x > tail_knot_x))
			{
				// Intersection with tail projection possible. Only calculate if requested intersection_index
				float tail_knot_y = point_arr[1].y; // points[3];
				float tail_dy = point_arr[0].y - tail_knot_y;// points[1] - tail_knot_y;

				if (tail_dx * tail_dx > 1e-18f)
				{
					float t = (x - tail_knot_x) / tail_dx;
					if (t > 0.0f)
						return tail_knot_y + t * tail_dy;
				}
			}

			//int head_knot_arr_index = (point_count - 2) * 2;
			//int head_handle_arr_index = (point_count - 1) * 2;
			float head_knot_x = point_arr[point_arr.Size-2].x;// points[head_knot_arr_index];
			float head_dx = point_arr[point_arr.Size-1].x - head_knot_x;// points[head_handle_arr_index] - head_knot_x;

			if ((head_dx < 0 && x < head_knot_x) || (head_dx > 0 && x > head_knot_x))
			{
				// Intersection with head projection possible. Only calculate if requested intersection_index
				float head_knot_y = point_arr[point_arr.Size-2].y;// points[head_knot_arr_index + 1];
				float head_dy = point_arr[point_arr.Size-1].y - head_knot_y;// points[head_handle_arr_index + 1] - head_knot_y;
				if (head_dx * head_dx > 1e-18f)
				{
					float t = (x - head_knot_x) / head_dx;
					if (t > 0.0f)
						return head_knot_y + t * head_dy;
				}
			}

			int col = intersectionColumn(x);
			if (col >= 0 && col <= col_count)
			{
				const ImVector<int>& segments = col_segments[col];
				int segment_count = segments.size();

				for (int i = 0; i < segment_count; i++)
				{
					int segment_i = segments[i];
					const ImVec2& p0 = path[segment_i];
					const ImVec2& p1 = path[segment_i + 1];
					float p_min_x = std::min(p0.x, p1.x);
					float p_max_x = std::max(p0.x, p1.x);
					float dx = p_max_x - p_min_x;

					if (dx > 1e-9 && x >= p_min_x && x <= p_max_x)
					{
						float t = (x - p0.x) / dx;
						if (t >= 0.0f && t <= 1.0f)
							return p0.y + t * (p1.y - p0.y);
					}
				}
			}

			/// DEBUG TEST NAN
			//assert(false);
			if (col >= 0 && col <= col_count)
			{
				const ImVector<int>& segments = col_segments[col];
				int segment_count = segments.size();

				for (int i = 0; i < segment_count; i++)
				{
					int segment_i = segments[i];
					const ImVec2& p0 = path[segment_i];
					const ImVec2& p1 = path[segment_i + 1];
					float p_min_x = std::min(p0.x, p1.x);
					float p_max_x = std::max(p0.x, p1.x);
					float dx = p_max_x - p_min_x;

					if (dx > 1e-9 && x >= p_min_x && x <= p_max_x)
					{
						float t = (x - p0.x) / dx;
						if (t >= 0.0f && t <= 1.0f)
							return p0.y + t * (p1.y - p0.y);
					}
				}
			}

			return std::numeric_limits<float>::quiet_NaN();
		}

		inline float operator()(float x) const
		{
			return firstIntersectY(x);
		}

		inline float operator()(float x, int i) const
		{
			return intersectY(x, i);
		}

		bool getLinearGradientIntercept(float& gradient, float& intercept, float tolerance = 1e-9)
		{
			ImVec2* points = pointVecArray();
			int n = point_arr.Size;
			if (n < 2) {
				//cerr << "Insufficient number of points to determine a line." << endl;
				return false;  // At least two points are required.
			}

			// Locate the first pair of points with a sufficient difference in x to avoid division by zero.
			int i = 0;
			while (i < n - 1 && fabs(points[i + 1].x - points[i].x) < tolerance) {
				++i;
			}
			if (i == n - 1) {
				//cerr << "All x-coordinates are nearly identical; cannot determine a valid gradient." << endl;
				return false;
			}

			// Calculate the reference gradient using the pair found.
			gradient = (points[i + 1].y - points[i].y) / (points[i + 1].x - points[i].x);

			// Use the point at index i as the reference to compute the intercept.
			float x0 = points[i].x;
			float y0 = points[i].y;
			intercept = y0 - gradient * x0;

			// Validate each point in the dataset.
			for (int i=0; i< point_arr.Size; i++)
			{
				ImVec2& p = points[i];
				// Calculate the expected y-value using the full line equation.
				float expected_y = gradient * p.x + intercept;
				if (fabs(p.y - expected_y) > tolerance) {
					return false;  // This point does not lie on the reference line.
				}
			}

			return true;
		}

		bool isSimpleLinear()
		{
			return optimization_type == OptimizationType::LINEAR;
		}

		bl::hash_t hash() const
		{
			return spline_hash;
		}

		// Base64 serialization/deserialization

		/*std::string serialize(bool base64=true) const
		{
			if (base64)
			{
				// Calculate total size
				size_t dataSize = sizeof(int) + (point_count * 2 * sizeof(float));
				std::vector<unsigned char> buffer(dataSize);

				// Copy data to buffer
				std::memcpy(buffer.data(), &point_count, sizeof(int));
				if (point_count > 0 && points)
					std::memcpy(buffer.data() + sizeof(int), points, point_count * 2 * sizeof(float));

				// Encode
				return base64_encode(buffer.data(), buffer.size());
			}
			else
			{
				
			}
		}
		
		static size_t deserializePointCount(const std::string& base64Str)
		{
			// Decode
			std::vector<unsigned char> buffer = base64_decode(base64Str);

			// Read only point_count
			size_t point_count;
			std::memcpy(&point_count, buffer.data(), sizeof(int));

			return point_count;
		}


		void deserialize(const std::string& base64Str)
		{
			bl::hash_t old_hash = hash();

			// Decode
			std::vector<unsigned char> buffer = base64_decode(base64Str);

			// Read data
			std::memcpy(&point_count, buffer.data(), sizeof(int));
			if (point_count > 0)
				std::memcpy(points, buffer.data() + sizeof(int), point_count * 2 * sizeof(float));
			else
				points = nullptr;

			updateHash();

			if (hash() != old_hash)
			{
				onChanged();
			}
		}*/

		std::string floatToCleanString(float value, int max_decimal_places, float precision, bool minimize) const
		{
			// Optional snapping
			if (precision > 0.0f) {
				value = std::round(value / precision) * precision;
			}

			// Use fixed formatting
			std::ostringstream oss;
			oss << std::fixed << std::setprecision(max_decimal_places) << value;
			std::string str = oss.str();

			// Remove trailing zeros
			size_t dot = str.find('.');
			if (dot != std::string::npos) {
				size_t last_non_zero = str.find_last_not_of('0');
				if (last_non_zero != std::string::npos) {
					str.erase(last_non_zero + 1);
				}
				// Remove trailing dot
				if (str.back() == '.') {
					str.pop_back();
				}
			}

			if (minimize)
			{
				// 4a. Collapse “-0” to “0”
				if (str == "-0") {
					str = "0";
				}

				// 4b. Remove the leading ‘0’ for numbers between -1 and 1
				//     (e.g. "0.7351"  -> ".7351",   "-0.42" -> "-.42")
				if (!str.empty()) {
					bool negative = (str[0] == '-');
					std::size_t first = negative ? 1 : 0;
					if (first + 1 < str.size() && str[first] == '0' && str[first + 1] == '.') {
						str.erase(first, 1);
					}
				}
			}

			return str;
		}

		//std::string serialize(bool compress=true, int try_shortest = false) const
		std::string serialize(SplineSerializationMode mode, int decimal_places=12) const
		{
			// First try binary
			std::string base64_txt;
			std::string raw_txt;

			int point_count = point_arr.Size;
			float* points = floatArray();

			bool compressed = mode == SplineSerializationMode::COMPRESS_BASE64 || mode == SplineSerializationMode::COMPRESS_SHORTEST;
			bool cpp_readable = mode == SplineSerializationMode::CPP_ARRAY;

			// Base-64
			//if (compress)
			if (compressed)
			{
				std::ostringstream oss(std::ios::binary);

				// Write point count
				oss.write(reinterpret_cast<const char*>(&point_count), sizeof(int));

				// Write points array if it exists
				if (point_count > 0 && points)
				{
					oss.write(reinterpret_cast<const char*>(points), point_count * 2 * sizeof(float));
				}

				// Convert stream to string
				std::string binaryData = oss.str();

				// Encode as base64
				base64_txt = base64_encode(reinterpret_cast<const unsigned char*>(binaryData.data()), binaryData.size());
				if (mode != SplineSerializationMode::COMPRESS_SHORTEST)
					return "B" + base64_txt;
			}
			
			// Text
			{
				std::ostringstream oss;
				if (cpp_readable)
					oss << "{";
				else
					oss << point_count << "_";
				int flt_count = point_count * 2;
				for (int i = 0; i < flt_count; i+=2)
				{
					if (cpp_readable) oss << "{";
					oss << floatToCleanString(points[i], decimal_places, 1e-14f, compressed);
					oss << (cpp_readable ? "," : "_");
					oss << floatToCleanString(points[i+1], decimal_places, 1e-14f, compressed);
					if (cpp_readable) oss << "}";

					if (i < flt_count-2) oss << (cpp_readable? "," : "_");
				}
				if (cpp_readable)
					oss << "}";
				raw_txt = oss.str();
			}

			// Compare shortest
			if (!compressed || raw_txt.size() < base64_txt.size())
			{
				return (cpp_readable ? "" : "A") + raw_txt;
			}
			else
			{
				return "B" + base64_txt;
			}
		}

		static size_t deserializePointCount(const std::string& txt)
		{
			if (txt[0] == 'B')
			{
				std::string base64 = txt.substr(1);
				std::vector<unsigned char> buffer = base64_decode(base64);
				std::istringstream iss(std::string(buffer.begin(), buffer.end()), std::ios::binary);

				int count = 0;
				iss.read(reinterpret_cast<char*>(&count), sizeof(int));

				return static_cast<size_t>(count);
			}
			else
			{
				std::string t = txt.substr(1);
				std::replace(t.begin(), t.end(), '_', ' '); // todo: use getline and skip this
				std::istringstream iss(txt);
				iss.ignore(); // Skip first char
				int point_count;
				iss >> point_count;
				return point_count;
			}
		}

		// Deserialize Base64 string to Spline
		void deserialize(const std::string& txt)
		{
			bl::hash_t old_hash = hash();

			int point_count = point_arr.Size;

			

			// Determine serialization type
			if (txt[0] == 'B')
			{
				std::string base64 = txt.substr(1);
				std::vector<unsigned char> buffer = base64_decode(base64);
				std::istringstream iss(std::string(buffer.begin(), buffer.end()), std::ios::binary);

				// Read point_count
				iss.read(reinterpret_cast<char*>(&point_count), sizeof(int));
				point_arr.resize(point_count);

				float* points = floatArray();

				// Allocate or nullify points based on count
				if (point_count > 0)
				{
					//if (!points)
					//	points = new float[point_count * 2]; // You must manage this externally

					iss.read(reinterpret_cast<char*>(points), point_count * 2 * sizeof(float));
				}
				//else
				//{
				//	points = nullptr;
				//}
			}
			else
			{
				std::string t = txt;
				std::replace(t.begin(), t.end(), '_', ' ');
				std::istringstream iss(t);
				iss.ignore(); // Skip first char

				iss >> point_count;
				point_arr.resize(point_count);

				float* points = floatArray();

				if (point_count > 0)
				{
					for (int i = 0; i < point_count; i++)
					{
						iss >> points[i * 2];
						iss >> points[i * 2 + 1];
					}
				}
				//else
				//{
				//	points = nullptr;
				//}
			}

			updateHash();

			if (hash() != old_hash)
			{
				onChanged();
			}
		}

		friend bool BeginSplineEditor(const char* label, Spline* spline, ImRect* grid_r, float max_editor_size, ImSplineFlags _flags);
		friend void EndSplineEditor();
	};


	//inline ImColor adjustBrightness(ImColor c, float mult)
	//{
	//	ImVec4 ret = c.Value * 0.9f;
	//	ret.w = c.Value.w;
	//	return ret;
	//}

	

	static inline ImColor green(0.0f, 1.0f, 0.0f, 1.0);
	static inline ImColor pink(1.0f, 0.0f, 0.75f, 1.0);
	static inline ImColor red_dim(1.0f, 0.0f, 0.0f, 0.5);
	static inline ImColor red(1.0f, 0.0f, 0.0f, 1.0);
	static inline ImColor blue(0.2f, 0.1f, 1.0f, 1.0);
	static inline ImColor cyan_inactive(0.00f, 0.5f, 1.0f, 1.0f);
	static inline ImColor cyan_active(0.0f, 0.75f, 1.0f, 1.0f);
	static inline ImColor grid_color(1.0f, 1.0f, 1.0f, 0.1f);
	static inline ImColor bright_grid_color(1.0f, 1.0f, 1.0f, 0.3f);
	static inline ImColor white(1.0f, 1.0f, 1.0f, 1.0f);
	static inline ImColor white_faded(1.0f, 1.0f, 1.0f, 0.4f);
	static inline ImColor light_gray(0.7f, 0.7f, 0.7f, 1.0f);

	static inline ImColor spline_path_color = white_faded;// GetColorU32(ImGuiCol_SliderGrab, 1);
	static inline ImColor handle_line_color = white;
	static inline ImColor handle_grabber_color = white;
	static inline ImColor handle_grabber_pressed_color = light_gray;

	using GraphTransformFn = ImVec2(*)(ImVec2);
	static inline GraphTransformFn fromGraph = nullptr;
	static inline GraphTransformFn toGraph = nullptr;

	static inline Spline* spline = nullptr;
	static inline ImRect* view_rect = nullptr;
	static inline ImRect bb{};
	static inline const char* label = nullptr;
	static inline bool active = false;
	static inline bool spline_changed = false;
	static inline ImSplineFlags flags = 0;

	inline ImVec2 fromGraphNegY(ImVec2 p)
	{
		ImVec2 r = (p - view_rect->Min) / (view_rect->Max - view_rect->Min);
		return bb.Min + (r * bb.GetSize());
	};
	inline ImVec2 toGraphNegY(ImVec2 p)
	{
		ImVec2 r = (p - bb.Min) / (bb.Max - bb.Min);
		return view_rect->Min + (r * view_rect->GetSize());
	};
	inline ImVec2 fromGraphY(ImVec2 p)
	{
		ImVec2 r = (p - view_rect->Min) / (view_rect->Max - view_rect->Min);
		r.y = 1.0f - r.y;
		return bb.Min + (r * bb.GetSize());
	}
	inline ImVec2 toGraphY(ImVec2 p)
	{
		ImVec2 r = (p - bb.Min) / (bb.Max - bb.Min);
		r.y = 1.0f - r.y;
		return view_rect->Min + (r * view_rect->GetSize());
	}

	inline void snap(ImVec2& p, float step)
	{
		p.x = floor(p.x / step) * step;
		p.y = floor(p.y / step) * step;
	};

	inline bool BeginSplineEditor(const char* _label, Spline* _spline, ImRect* _view_rect, float max_editor_size = 300.0f, ImSplineFlags _flags=0)
	{
		using namespace ImGui;

		label = _label;
		spline = _spline;
		view_rect = _view_rect;
		flags = _flags;

		const bool invert_y = (_flags & ImSplineFlags_InvertY) != 0;

		//const ImGuiStyle& Style = GetStyle();
		const ImGuiIO& IO = GetIO();
		ImDrawList* DrawList = GetWindowDrawList();
		ImGuiWindow* Window = GetCurrentWindow();
		if (Window->SkipItems)
			return false;

		// prepare canvas
		float dim = ImGui::GetContentRegionAvail().x;
		if (dim > max_editor_size)
			dim = max_editor_size;

		ImVec2 Canvas(dim, dim);
		bb = ImRect(Window->DC.CursorPos, Window->DC.CursorPos + Canvas);
		ItemSize(bb);
		if (!ItemAdd(bb, 0))
			return false;



		//hovered |= 0 != ItemHoverable(bb, id, ImGui::GetCurrentContext()->LastItemData.ItemFlags);
		//int hovered = IsItemActive() || IsItemHovered();
		//const bool hovered = IsItemActive() || ItemHoverable(bb, id, ImGui::GetCurrentContext()->LastItemData.ItemFlags);

		ImVec4 bg = ImGui::GetStyleColorVec4(ImGuiCol_ChildBg);
		ImVec4 dim_bg = ImVec4(bg.x * 0.85f, bg.y * 0.85f, bg.z * 0.85f, bg.w);

		const ImGuiID id = Window->GetID(label);

		bool hovered;
		ButtonBehavior(bb, id, &hovered, &active, ImGuiButtonFlags_MouseButtonLeft);

		active |= (spline->dragging_index >= 0);

		// draw with Window->ClipRect automatically clipping anything outside the window
		RenderFrame(bb.Min, bb.Max, GetColorU32(dim_bg));

		
		spline_changed = false;

		fromGraph = (_flags & ImSplineFlags_InvertY) ? &fromGraphY : &fromGraphNegY;
		toGraph = (_flags & ImSplineFlags_InvertY) ? &toGraphY : &toGraphNegY;

		//float* points = spline->points;
		int numPoints = spline->point_arr.Size;

		ImVec2 pointer = IO.MousePos;
		ImVec2 graph_mouse = toGraph(pointer);

		// Handle knot/handle dragging
		//float handle_size = ImGui::GetFontSize() / 4.0f;
		//float handle_size_sq = handle_size * handle_size;

		// Cast float* to ImVec2*
		ImVec2* point_arr = spline->pointVecArray();

		if (active || hovered)
		{
			// Handle scroll-wheel zoom
			ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
			if (ImGui::TestKeyOwner(ImGuiKey_MouseWheelY, id))
			{
				ImGui::SetKeyOwner(ImGuiKey_MouseWheelY, id);

				float scroll = IO.MouseWheel;
				if (scroll != 0.0f)
				{
					float scale = 1.0f - scroll * 0.1f;
					ImVec2 cen = view_rect->GetCenter();
					view_rect->Min = cen + (view_rect->Min - cen) * scale;
					view_rect->Max = cen + (view_rect->Max - cen) * scale;
				}
			}

			// Handle panning
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
			{
				spline->panning = true;
				spline->pan_mouse_down_vr = *view_rect;
				spline->pan_mouse_down_pos = pointer;
			}
			else if (
				ImGui::IsMouseReleased(ImGuiMouseButton_Middle) ||
				!ImGui::IsMouseDown(ImGuiMouseButton_Middle))
			{
				spline->panning = false;
			}

			if (spline->panning)
			{
				ImVec2 pixel_offset = (pointer - spline->pan_mouse_down_pos);
				//ImVec2 graph_offset = pixel_offset * (view_rect->Max - view_rect->Min) / bb.GetSize();

				ImVec2 view_span = (view_rect->Max - view_rect->Min);
				ImVec2 bb_size = bb.GetSize();
				ImVec2 graph_offset = pixel_offset * view_span / bb_size;
				if (invert_y) graph_offset.y = -graph_offset.y;

				view_rect->Min = spline->pan_mouse_down_vr.Min - graph_offset;
				view_rect->Max = spline->pan_mouse_down_vr.Max - graph_offset;
			}


			//RenderFrame(bb.Min, bb.Max, GetColorU32(ImGuiCol_FrameBg, 1), true, Style.FrameRounding);
			///RenderFrame(bb.Min, bb.Max, GetColorU32(dim_bg), true, Style.FrameRounding);

			if (IsMouseClicked(0) && bb.Contains(pointer))
			{
				float nearest_d2 = std::numeric_limits<float>::max();
				int nearest_i = -1;
				for (int i = 0; i < numPoints; i++)
				{
					ImVec2& p = point_arr[i];
					ImVec2 client_pos = fromGraph(p);
					ImVec2 mouse_dist = pointer - client_pos;
					float d2 = mouse_dist.x * mouse_dist.x + mouse_dist.y * mouse_dist.y;
					if (d2 < nearest_d2)
					{
						nearest_d2 = d2;
						nearest_i = i;
					}
				}
				if (nearest_i >= 0)
				{
					spline->dragging_index = nearest_i;
					ImVec2& p = point_arr[nearest_i];

					bool isKnot = spline->isKnot(nearest_i);
					if (isKnot)
					{
						// Clicked on knot
						ImVec2& h1_p = point_arr[nearest_i - 1];
						ImVec2& h2_p = point_arr[nearest_i + 1];
						spline->h1_offset = h1_p - p;
						spline->h2_offset = h2_p - p;
					}
					else
					{
						// Clicked on handle. Get opposite handle dist for rotation
						int knot_point_index = (nearest_i / 3) * 3 + 1;
						int h1_index = knot_point_index - 1;
						int h2_index = knot_point_index + 1;
						ImVec2& knot = point_arr[knot_point_index];

						if (nearest_i == h1_index)
						{
							// handle h1 clicked, cache h2 dist
							ImVec2& h2 = point_arr[h2_index];
							ImVec2 h2_d = h2 - knot;
							spline->opposite_handle_dist = sqrt(h2_d.x * h2_d.x + h2_d.y * h2_d.y);

						}
						else if (nearest_i == h2_index)
						{
							// handle h2 clicked, cache h1 dist
							ImVec2& h1 = point_arr[h1_index];
							ImVec2 h1_d = h1 - knot;
							spline->opposite_handle_dist = sqrt(h1_d.x * h1_d.x + h1_d.y * h1_d.y);
						}
					}
				}
			}
			else if (IsMouseReleased(0))
			{
				spline->dragging_index = -1;
			}

			for (int i = 0; i < numPoints; i++)
			{
				ImVec2& p = point_arr[i];
				//ImVec2 client_pos = fromGraph(p);
				//ImVec2 mouse_dist = pointer - client_pos;
				//bool hit = (i == spline->dragging_index);

				bool isKnot = spline->isKnot(i);
				bool dragging_point = (spline->dragging_index == i);

				if (dragging_point)
				{
					p.x = graph_mouse.x;
					p.y = graph_mouse.y;
					snap(p, spline->xy_precision);

					// If knot, move connected handles
					if (isKnot)
					{
						point_arr[i - 1] = p + spline->h1_offset;
						point_arr[i + 1] = p + spline->h2_offset;
						snap(point_arr[i - 1], spline->xy_precision);
						snap(point_arr[i + 1], spline->xy_precision);
					}
					else // If handle, rotate opposite handle around knot
					{
						int knot_point_index = (i / 3) * 3 + 1;
						int h1_index = knot_point_index - 1;
						int h2_index = knot_point_index + 1;
						ImVec2& knot = point_arr[knot_point_index];

						if (i == h1_index)
						{
							// handle h1 dragged, rotate h2
							ImVec2& h1 = point_arr[h1_index];
							float h2_angle = atan2(h1.y - knot.y, h1.x - knot.x) + IM_PI;
							point_arr[h2_index].x = knot.x + cos(h2_angle) * spline->opposite_handle_dist;
							point_arr[h2_index].y = knot.y + sin(h2_angle) * spline->opposite_handle_dist;
							snap(point_arr[h2_index], spline->xy_precision);
						}
						else if (i == h2_index)
						{
							// handle h2 dragged, rotate h1
							ImVec2& h2 = point_arr[h2_index];
							float h1_angle = atan2(h2.y - knot.y, h2.x - knot.x) + IM_PI;
							point_arr[h1_index].x = knot.x + cos(h1_angle) * spline->opposite_handle_dist;
							point_arr[h1_index].y = knot.y + sin(h1_angle) * spline->opposite_handle_dist;
							snap(point_arr[h1_index], spline->xy_precision);
						}
					}

					spline_changed = true;
				}
			}
		}

		// Build intersection map
		if (spline_changed || spline->col_segments_dirty)
		{
			spline->onChanged();
		}

		//ImVector<ImVec2>& path = spline->path;
		ImVec2 clip_min = bb.Min;
		ImVec2 clip_max = bb.Max;
		DrawList->PushClipRect(clip_min, clip_max, true);

		// Draw grid
		//ImGui::GetFont()->Scale = bb.GetWidth() / 200.0f;
		float old_font_scale = GetCurrentWindow()->FontWindowScale;
		ImGui::SetWindowFontScale(std::min(old_font_scale, std::max(0.5f * old_font_scale, old_font_scale * (bb.GetWidth() / 200.0f))));

		auto roundStep = [](float ideal_step)
		{
			float abs_ideal_step = fabs(ideal_step);
			float exponent = floor(log10(abs_ideal_step));
			float factor = powf(10.0f, exponent);
			float base = abs_ideal_step / factor;
			float niceMultiplier;

			if (base >= 10.0f) niceMultiplier = 10.0f;
			else if (base >= 5.0f) niceMultiplier = 5.0f;
			else if (base >= 2.5f) niceMultiplier = 2.5f;
			else if (base >= 2.0f) niceMultiplier = 2.0f;
			else niceMultiplier = 1.0f;

			return niceMultiplier * factor * (ideal_step < 0.0f ? -1.0f : 1.0f);
		};

		float step_x = roundStep(view_rect->GetWidth() / (bb.GetWidth() / 100.0f));
		float step_y = roundStep(view_rect->GetHeight() / (bb.GetHeight() / 100.0f));
		float grid_x = floor(view_rect->Min.x / step_x) * step_x;
		float grid_y = floor(view_rect->Min.y / step_y) * step_y;
		int grid_count_x = 2 + static_cast<int>(floor(fabs(view_rect->GetWidth()) / fabs(step_x)));
		int grid_count_y = 2 + static_cast<int>(floor(fabs(view_rect->GetHeight()) / fabs(step_y)));
		float eps = 1e-5f;
		float y_denom = (view_rect->Max.y - view_rect->Min.y);

		for (int i = 0; i < grid_count_x; i++)
		{
			bool is_origin = fabs(grid_x) < eps;
			ImColor color = is_origin ? bright_grid_color : grid_color;

			float px = bb.Min.x + ((grid_x - view_rect->Min.x) / (view_rect->Max.x - view_rect->Min.x) * bb.GetWidth());
			px = floorf(px);

			DrawList->AddLine(ImVec2(px, bb.Min.y), ImVec2(px, bb.Max.y), color, 1);

			if (px > bb.Min.x + 10)
			{
				char txt[64];
				snprintf(txt, sizeof(txt), "%.1f", is_origin ? eps : grid_x);
				DrawList->AddText(ImVec2(px, bb.Min.y), white, txt);
			}

			grid_x += step_x;
		}
		for (int i = 0; i < grid_count_y; i++)
		{
			bool is_origin = fabs(grid_y) < eps;
			ImColor color = is_origin ? ImColor(bright_grid_color) : grid_color;

			//float py = bb.Min.y + ((grid_y - view_rect->Min.y) / (view_rect->Max.y - view_rect->Min.y) * bb.GetHeight());
			//py = floorf(py);

			float t = (grid_y - view_rect->Min.y) / y_denom;
			if (invert_y) t = 1.0f - t;

			float py = bb.Min.y + t * bb.GetHeight();
			py = floorf(py);

			DrawList->AddLine(ImVec2(bb.Min.x, py), ImVec2(bb.Max.x, py), color, 1);

			if (py > bb.Min.y + 10)
			{
				char txt[64];
				snprintf(txt, sizeof(txt), "%.1f", is_origin ? eps : grid_y);
				DrawList->AddText(ImVec2(bb.Min.x, py), ImColor(white), txt);
			}

			grid_y += step_y;
		}

		ImGui::SetWindowFontScale(old_font_scale);

		return true;// spline_changed;
	}

	inline void EndSplineEditor()
	{
		using namespace ImGui;

		ImDrawList* DrawList = GetWindowDrawList();

		if (spline->equation)
		{
			float eq_inc = view_rect->GetWidth() / 100.0f;
			ImVector<ImVec2> eq_points;
			for (float x = view_rect->Min.x; x <= view_rect->Max.x; x += eq_inc)
			{
				float y = spline->equation(x);
				eq_points.push_back(fromGraph({ x, y }));
			}
			DrawList->AddPolyline(eq_points.Data, eq_points.Size, red, false, 1);
		}

		//float spline_thickness = ImGui::GetFontSize() / 10.0f;
		const ImGuiIO& IO = GetIO();
		ImGuiWindow* Window = GetCurrentWindow();
		const ImGuiID id = Window->GetID(label);

		ImVec2* point_arr = spline->pointVecArray();
		ImVector<ImVec2>& path = spline->path;
		int numPoints = spline->point_arr.Size;
		float handle_size = ImGui::GetFontSize() / 4.0f;
		ImVec2 pointer = IO.MousePos;
		ImVec2 graph_mouse = toGraph(pointer);

		// Draw Spline
		ImVector<ImVec2> spline_path;
		//spline_path.push_back(fromGraph({ 0.0f, spline->intersectY(0.0f) }));
		for (int i = 0; i < path.size(); i++)
			spline_path.push_back(fromGraph(path[i]));
		DrawList->AddPolyline(spline_path.Data, spline_path.Size, spline_path_color, false, 4);

		// Draw handle lines
		for (size_t knot_point_index = 1; knot_point_index < numPoints; knot_point_index += 3)
		{
			ImVec2& knot = point_arr[knot_point_index];
			ImVec2& h1 = point_arr[knot_point_index - 1];
			ImVec2& h2 = point_arr[knot_point_index + 1];
			ImVec2 knot_pos = fromGraph(knot);
			DrawList->AddLine(knot_pos, fromGraph(h1), handle_line_color, 1);
			DrawList->AddLine(knot_pos, fromGraph(h2), handle_line_color, 1);
		}

		// Draw handle circles
		for (int i = 0; i < numPoints; i++)
		{
			bool dragging_point = (spline->dragging_index == i);
			DrawList->AddCircleFilled(fromGraph(point_arr[i]), handle_size, dragging_point ? white : light_gray);
		}

		// Show intersections
		if (spline->dragging_index < 0)
		{
			int intersection_count = spline->countIntersectsY(graph_mouse.x);
			DrawList->AddLine(ImVec2(pointer.x, bb.Min.y), ImVec2(pointer.x, bb.Max.y), red_dim, 1.0f);
			for (int i = 0; i < intersection_count; i++)
			{
				float iy = spline->intersectY(graph_mouse.x, i);
				DrawList->AddCircleFilled(fromGraph(ImVec2(graph_mouse.x, iy)), 4, red);
			}
		}

		DrawList->PopClipRect();

		if (active)
		{
			SetActiveID(id, ImGui::GetCurrentWindow());
			if (spline_changed)
				MarkItemEdited(id);
		}
	}

	inline void PlotPoint(float x, float y, float r=3.0f, ImColor col=red)
	{
		using namespace ImGui;
		ImDrawList* DrawList = GetWindowDrawList();
		DrawList->AddCircleFilled(fromGraph(ImVec2(x,y)), r, col);
	}

	inline bool SplineEditor(const char* _label, Spline* _spline, ImRect* _view_rect, float max_editor_size = 300.0f, ImSplineFlags _flags = 0)
	{
		bool changed = BeginSplineEditor(_label, _spline, _view_rect, max_editor_size, _flags);
		EndSplineEditor();
		return changed;
	}

	//inline bool SplineEditorPair(
	//	const char* label,
	//	Spline* spline1,
	//	Spline* spline2,
	//	ImRect* view_rect,
	//	float max_graph_width = 500.0f,
	//	float min_graph_wrap_width = 150.0f)
	//{
	//	UNUSED(label);
	//	UNUSED(spline1);
	//	UNUSED(spline2);
	//	UNUSED(view_rect);
	//	UNUSED(max_graph_width);
	//	UNUSED(min_graph_wrap_width);
	//
	//
	//	float avail_width = ImGui::GetContentRegionAvail().x;
	//	float spacing = ImGui::GetStyle().ItemSpacing.x;
	//
	//	// Clamp the available width to max_graph_width if necessary.
	//	float clamped_width = (avail_width > max_graph_width) ? max_graph_width : avail_width;
	//	float graph_size = (clamped_width - spacing) / 2.0f;
	//
	//
	//	char label1[128], label2[128];
	//	snprintf(label1, sizeof(label1), "%s##1", label);
	//	snprintf(label2, sizeof(label2), "%s##2", label);
	//
	//	if (graph_size < min_graph_wrap_width)
	//	{
	//		bool ret1 = SplineEditor(label1, spline1, view_rect, avail_width);
	//		bool ret2 = SplineEditor(label2, spline2, view_rect, avail_width);
	//		return ret1 || ret2;
	//	}
	//	else
	//	{
	//		bool ret1 = SplineEditor(label1, spline1, view_rect, graph_size);
	//		ImGui::SameLine(0, spacing);
	//		bool ret2 = SplineEditor(label2, spline2, view_rect, graph_size);
	//		return ret1 || ret2;
	//	}
	//	return false;
	//}
}
