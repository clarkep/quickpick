/*
 * Copyright (c) 2013-14 Mikko Mononen memon@inside.org
 *
 * This software is provided 'as-is', without any express or implied
 * warranty.  In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 * claim that you wrote the original software. If you use this software
 * in a product, an acknowledgment in the product documentation would be
 * appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 * misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 *
 * The polygon rasterization is heavily based on stb_truetype rasterizer
 * by Sean Barrett - http://nothings.org/
 *
 */

#ifndef AU_NANOSVGRAST_H
#define AU_NANOSVGRAST_H

#include "nanosvg.h"

#ifndef AU_NANOSVGRAST_CPLUSPLUS
#ifdef __cplusplus
extern "C" {
#endif
#endif

typedef struct ANSVGrasterizer ANSVGrasterizer;

/* Example Usage:
	// Load SVG
	NSVGimage* image;
	image = nsvgParseFromFile("test.svg", "px", 96);

	// Create rasterizer (can be used to render multiple images).
	struct ANSVGrasterizer* rast = ansvgCreateRasterizer();
	// Allocate memory for image
	unsigned char* img = malloc(w*h*4);
	// Rasterize
	ansvgRasterize(rast, image, 0,0,1, img, w, h, w*4);
*/

// Allocated rasterizer context.
ANSVGrasterizer* ansvgCreateRasterizer(void);

enum ANSVGrenderMode {
	ANSVG_RENDER_ALPHA,
	ANSVG_RENDER_RGBA,
	ANSVG_RENDER_BGRA
};

// Rasterizes SVG image, returns RGBA image (non-premultiplied alpha)
//   r - pointer to rasterizer context
//   image - pointer to image to rasterize
//   tx,ty - image offset (applied after scaling)
//   scale - image scale
//   dst - pointer to destination image data, 4 bytes per pixel (RGBA)
//   w - width of the image to render
//   h - height of the image to render
//   stride - number of bytes per scaleline in the destination buffer
void ansvgRasterize(ANSVGrasterizer* r,
				   NSVGimage* image, float tx, float ty, float scale,
				   unsigned char* dst, int w, int h, int stride, enum ANSVGrenderMode);

// Deletes rasterizer context.
void ansvgDeleteRasterizer(ANSVGrasterizer*);


#ifndef AU_NANOSVGRAST_CPLUSPLUS
#ifdef __cplusplus
}
#endif
#endif

#ifdef AU_NANOSVGRAST_IMPLEMENTATION

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define ANSVG__SUBSAMPLES	5
#define ANSVG__FIXSHIFT		10
#define ANSVG__FIX			(1 << ANSVG__FIXSHIFT)
#define ANSVG__FIXMASK		(ANSVG__FIX-1)
#define ANSVG__MEMPAGE_SIZE	1024

typedef struct ANSVGedge {
	float x0,y0, x1,y1;
	int dir;
	struct ANSVGedge* next;
} ANSVGedge;

typedef struct ANSVGpoint {
	float x, y;
	float dx, dy;
	float len;
	float dmx, dmy;
	unsigned char flags;
} ANSVGpoint;

typedef struct ANSVGactiveEdge {
	int x,dx;
	float ey;
	int dir;
	struct ANSVGactiveEdge *next;
} ANSVGactiveEdge;

typedef struct ANSVGmemPage {
	unsigned char mem[ANSVG__MEMPAGE_SIZE];
	int size;
	struct ANSVGmemPage* next;
} ANSVGmemPage;

typedef struct ANSVGcachedPaint {
	signed char type;
	char spread;
	float xform[6];
	unsigned int colors[256];
} ANSVGcachedPaint;

struct ANSVGrasterizer
{
	float px, py;

	float tessTol;
	float distTol;

	ANSVGedge* edges;
	int nedges;
	int cedges;

	ANSVGpoint* points;
	int npoints;
	int cpoints;

	ANSVGpoint* points2;
	int npoints2;
	int cpoints2;

	ANSVGactiveEdge* freelist;
	ANSVGmemPage* pages;
	ANSVGmemPage* curpage;

	unsigned char* scanline;
	int cscanline;

	unsigned char* bitmap;
	int width, height, stride;
};

ANSVGrasterizer* ansvgCreateRasterizer(void)
{
	ANSVGrasterizer* r = (ANSVGrasterizer*)malloc(sizeof(ANSVGrasterizer));
	if (r == NULL) goto error;
	memset(r, 0, sizeof(ANSVGrasterizer));

	r->tessTol = 0.25f;
	r->distTol = 0.01f;

	return r;

error:
	ansvgDeleteRasterizer(r);
	return NULL;
}

void ansvgDeleteRasterizer(ANSVGrasterizer* r)
{
	ANSVGmemPage* p;

	if (r == NULL) return;

	p = r->pages;
	while (p != NULL) {
		ANSVGmemPage* next = p->next;
		free(p);
		p = next;
	}

	if (r->edges) free(r->edges);
	if (r->points) free(r->points);
	if (r->points2) free(r->points2);
	if (r->scanline) free(r->scanline);

	free(r);
}

static ANSVGmemPage* ansvg__nextPage(ANSVGrasterizer* r, ANSVGmemPage* cur)
{
	ANSVGmemPage *newp;

	// If using existing chain, return the next page in chain
	if (cur != NULL && cur->next != NULL) {
		return cur->next;
	}

	// Alloc new page
	newp = (ANSVGmemPage*)malloc(sizeof(ANSVGmemPage));
	if (newp == NULL) return NULL;
	memset(newp, 0, sizeof(ANSVGmemPage));

	// Add to linked list
	if (cur != NULL)
		cur->next = newp;
	else
		r->pages = newp;

	return newp;
}

static void ansvg__resetPool(ANSVGrasterizer* r)
{
	ANSVGmemPage* p = r->pages;
	while (p != NULL) {
		p->size = 0;
		p = p->next;
	}
	r->curpage = r->pages;
}

static unsigned char* ansvg__alloc(ANSVGrasterizer* r, int size)
{
	unsigned char* buf;
	if (size > ANSVG__MEMPAGE_SIZE) return NULL;
	if (r->curpage == NULL || r->curpage->size+size > ANSVG__MEMPAGE_SIZE) {
		r->curpage = ansvg__nextPage(r, r->curpage);
	}
	buf = &r->curpage->mem[r->curpage->size];
	r->curpage->size += size;
	return buf;
}

static int ansvg__ptEquals(float x1, float y1, float x2, float y2, float tol)
{
	float dx = x2 - x1;
	float dy = y2 - y1;
	return dx*dx + dy*dy < tol*tol;
}

static void ansvg__addPathPoint(ANSVGrasterizer* r, float x, float y, int flags)
{
	ANSVGpoint* pt;

	if (r->npoints > 0) {
		pt = &r->points[r->npoints-1];
		if (ansvg__ptEquals(pt->x,pt->y, x,y, r->distTol)) {
			pt->flags = (unsigned char)(pt->flags | flags);
			return;
		}
	}

	if (r->npoints+1 > r->cpoints) {
		r->cpoints = r->cpoints > 0 ? r->cpoints * 2 : 64;
		r->points = (ANSVGpoint*)realloc(r->points, sizeof(ANSVGpoint) * r->cpoints);
		if (r->points == NULL) return;
	}

	pt = &r->points[r->npoints];
	pt->x = x;
	pt->y = y;
	pt->flags = (unsigned char)flags;
	r->npoints++;
}

static void ansvg__appendPathPoint(ANSVGrasterizer* r, ANSVGpoint pt)
{
	if (r->npoints+1 > r->cpoints) {
		r->cpoints = r->cpoints > 0 ? r->cpoints * 2 : 64;
		r->points = (ANSVGpoint*)realloc(r->points, sizeof(ANSVGpoint) * r->cpoints);
		if (r->points == NULL) return;
	}
	r->points[r->npoints] = pt;
	r->npoints++;
}

static void ansvg__duplicatePoints(ANSVGrasterizer* r)
{
	if (r->npoints > r->cpoints2) {
		r->cpoints2 = r->npoints;
		r->points2 = (ANSVGpoint*)realloc(r->points2, sizeof(ANSVGpoint) * r->cpoints2);
		if (r->points2 == NULL) return;
	}

	memcpy(r->points2, r->points, sizeof(ANSVGpoint) * r->npoints);
	r->npoints2 = r->npoints;
}

static void ansvg__addEdge(ANSVGrasterizer* r, float x0, float y0, float x1, float y1)
{
	ANSVGedge* e;

	// Skip horizontal edges
	if (y0 == y1)
		return;

	if (r->nedges+1 > r->cedges) {
		r->cedges = r->cedges > 0 ? r->cedges * 2 : 64;
		r->edges = (ANSVGedge*)realloc(r->edges, sizeof(ANSVGedge) * r->cedges);
		if (r->edges == NULL) return;
	}

	e = &r->edges[r->nedges];
	r->nedges++;

	if (y0 < y1) {
		e->x0 = x0;
		e->y0 = y0;
		e->x1 = x1;
		e->y1 = y1;
		e->dir = 1;
	} else {
		e->x0 = x1;
		e->y0 = y1;
		e->x1 = x0;
		e->y1 = y0;
		e->dir = -1;
	}
}

static float ansvg__normalize(float *x, float* y)
{
	float d = sqrtf((*x)*(*x) + (*y)*(*y));
	if (d > 1e-6f) {
		float id = 1.0f / d;
		*x *= id;
		*y *= id;
	}
	return d;
}

static float ansvg__absf(float x) { return x < 0 ? -x : x; }
static float ansvg__roundf(float x) { return (x >= 0) ? floorf(x + 0.5) : ceilf(x - 0.5); }

static void ansvg__flattenCubicBez(ANSVGrasterizer* r,
								  float x1, float y1, float x2, float y2,
								  float x3, float y3, float x4, float y4,
								  int level, int type)
{
	float x12,y12,x23,y23,x34,y34,x123,y123,x234,y234,x1234,y1234;
	float dx,dy,d2,d3;

	if (level > 10) return;

	x12 = (x1+x2)*0.5f;
	y12 = (y1+y2)*0.5f;
	x23 = (x2+x3)*0.5f;
	y23 = (y2+y3)*0.5f;
	x34 = (x3+x4)*0.5f;
	y34 = (y3+y4)*0.5f;
	x123 = (x12+x23)*0.5f;
	y123 = (y12+y23)*0.5f;

	dx = x4 - x1;
	dy = y4 - y1;
	d2 = ansvg__absf((x2 - x4) * dy - (y2 - y4) * dx);
	d3 = ansvg__absf((x3 - x4) * dy - (y3 - y4) * dx);

	if ((d2 + d3)*(d2 + d3) < r->tessTol * (dx*dx + dy*dy)) {
		ansvg__addPathPoint(r, x4, y4, type);
		return;
	}

	x234 = (x23+x34)*0.5f;
	y234 = (y23+y34)*0.5f;
	x1234 = (x123+x234)*0.5f;
	y1234 = (y123+y234)*0.5f;

	ansvg__flattenCubicBez(r, x1,y1, x12,y12, x123,y123, x1234,y1234, level+1, 0);
	ansvg__flattenCubicBez(r, x1234,y1234, x234,y234, x34,y34, x4,y4, level+1, type);
}

static void ansvg__flattenShape(ANSVGrasterizer* r, NSVGshape* shape, float scale)
{
	int i, j;
	NSVGpath* path;

	for (path = shape->paths; path != NULL; path = path->next) {
		r->npoints = 0;
		// Flatten path
		ansvg__addPathPoint(r, path->pts[0]*scale, path->pts[1]*scale, 0);
		for (i = 0; i < path->npts-1; i += 3) {
			float* p = &path->pts[i*2];
			ansvg__flattenCubicBez(r, p[0]*scale,p[1]*scale, p[2]*scale,p[3]*scale, p[4]*scale,p[5]*scale, p[6]*scale,p[7]*scale, 0, 0);
		}
		// Close path
		ansvg__addPathPoint(r, path->pts[0]*scale, path->pts[1]*scale, 0);
		// Build edges
		for (i = 0, j = r->npoints-1; i < r->npoints; j = i++)
			ansvg__addEdge(r, r->points[j].x, r->points[j].y, r->points[i].x, r->points[i].y);
	}
}

enum ANSVGpointFlags
{
	ANSVG_PT_CORNER = 0x01,
	ANSVG_PT_BEVEL = 0x02,
	ANSVG_PT_LEFT = 0x04
};

static void ansvg__initClosed(ANSVGpoint* left, ANSVGpoint* right, ANSVGpoint* p0, ANSVGpoint* p1, float lineWidth)
{
	float w = lineWidth * 0.5f;
	float dx = p1->x - p0->x;
	float dy = p1->y - p0->y;
	float len = ansvg__normalize(&dx, &dy);
	float px = p0->x + dx*len*0.5f, py = p0->y + dy*len*0.5f;
	float dlx = dy, dly = -dx;
	float lx = px - dlx*w, ly = py - dly*w;
	float rx = px + dlx*w, ry = py + dly*w;
	left->x = lx; left->y = ly;
	right->x = rx; right->y = ry;
}

static void ansvg__buttCap(ANSVGrasterizer* r, ANSVGpoint* left, ANSVGpoint* right, ANSVGpoint* p, float dx, float dy, float lineWidth, int connect)
{
	float w = lineWidth * 0.5f;
	float px = p->x, py = p->y;
	float dlx = dy, dly = -dx;
	float lx = px - dlx*w, ly = py - dly*w;
	float rx = px + dlx*w, ry = py + dly*w;

	ansvg__addEdge(r, lx, ly, rx, ry);

	if (connect) {
		ansvg__addEdge(r, left->x, left->y, lx, ly);
		ansvg__addEdge(r, rx, ry, right->x, right->y);
	}
	left->x = lx; left->y = ly;
	right->x = rx; right->y = ry;
}

static void ansvg__squareCap(ANSVGrasterizer* r, ANSVGpoint* left, ANSVGpoint* right, ANSVGpoint* p, float dx, float dy, float lineWidth, int connect)
{
	float w = lineWidth * 0.5f;
	float px = p->x - dx*w, py = p->y - dy*w;
	float dlx = dy, dly = -dx;
	float lx = px - dlx*w, ly = py - dly*w;
	float rx = px + dlx*w, ry = py + dly*w;

	ansvg__addEdge(r, lx, ly, rx, ry);

	if (connect) {
		ansvg__addEdge(r, left->x, left->y, lx, ly);
		ansvg__addEdge(r, rx, ry, right->x, right->y);
	}
	left->x = lx; left->y = ly;
	right->x = rx; right->y = ry;
}

#ifndef ANSVG_PI
#define ANSVG_PI (3.14159265358979323846264338327f)
#endif

static void ansvg__roundCap(ANSVGrasterizer* r, ANSVGpoint* left, ANSVGpoint* right, ANSVGpoint* p, float dx, float dy, float lineWidth, int ncap, int connect)
{
	int i;
	float w = lineWidth * 0.5f;
	float px = p->x, py = p->y;
	float dlx = dy, dly = -dx;
	float lx = 0, ly = 0, rx = 0, ry = 0, prevx = 0, prevy = 0;

	for (i = 0; i < ncap; i++) {
		float a = (float)i/(float)(ncap-1)*ANSVG_PI;
		float ax = cosf(a) * w, ay = sinf(a) * w;
		float x = px - dlx*ax - dx*ay;
		float y = py - dly*ax - dy*ay;

		if (i > 0)
			ansvg__addEdge(r, prevx, prevy, x, y);

		prevx = x;
		prevy = y;

		if (i == 0) {
			lx = x; ly = y;
		} else if (i == ncap-1) {
			rx = x; ry = y;
		}
	}

	if (connect) {
		ansvg__addEdge(r, left->x, left->y, lx, ly);
		ansvg__addEdge(r, rx, ry, right->x, right->y);
	}

	left->x = lx; left->y = ly;
	right->x = rx; right->y = ry;
}

static void ansvg__bevelJoin(ANSVGrasterizer* r, ANSVGpoint* left, ANSVGpoint* right, ANSVGpoint* p0, ANSVGpoint* p1, float lineWidth)
{
	float w = lineWidth * 0.5f;
	float dlx0 = p0->dy, dly0 = -p0->dx;
	float dlx1 = p1->dy, dly1 = -p1->dx;
	float lx0 = p1->x - (dlx0 * w), ly0 = p1->y - (dly0 * w);
	float rx0 = p1->x + (dlx0 * w), ry0 = p1->y + (dly0 * w);
	float lx1 = p1->x - (dlx1 * w), ly1 = p1->y - (dly1 * w);
	float rx1 = p1->x + (dlx1 * w), ry1 = p1->y + (dly1 * w);

	ansvg__addEdge(r, lx0, ly0, left->x, left->y);
	ansvg__addEdge(r, lx1, ly1, lx0, ly0);

	ansvg__addEdge(r, right->x, right->y, rx0, ry0);
	ansvg__addEdge(r, rx0, ry0, rx1, ry1);

	left->x = lx1; left->y = ly1;
	right->x = rx1; right->y = ry1;
}

static void ansvg__miterJoin(ANSVGrasterizer* r, ANSVGpoint* left, ANSVGpoint* right, ANSVGpoint* p0, ANSVGpoint* p1, float lineWidth)
{
	float w = lineWidth * 0.5f;
	float dlx0 = p0->dy, dly0 = -p0->dx;
	float dlx1 = p1->dy, dly1 = -p1->dx;
	float lx0, rx0, lx1, rx1;
	float ly0, ry0, ly1, ry1;

	if (p1->flags & ANSVG_PT_LEFT) {
		lx0 = lx1 = p1->x - p1->dmx * w;
		ly0 = ly1 = p1->y - p1->dmy * w;
		ansvg__addEdge(r, lx1, ly1, left->x, left->y);

		rx0 = p1->x + (dlx0 * w);
		ry0 = p1->y + (dly0 * w);
		rx1 = p1->x + (dlx1 * w);
		ry1 = p1->y + (dly1 * w);
		ansvg__addEdge(r, right->x, right->y, rx0, ry0);
		ansvg__addEdge(r, rx0, ry0, rx1, ry1);
	} else {
		lx0 = p1->x - (dlx0 * w);
		ly0 = p1->y - (dly0 * w);
		lx1 = p1->x - (dlx1 * w);
		ly1 = p1->y - (dly1 * w);
		ansvg__addEdge(r, lx0, ly0, left->x, left->y);
		ansvg__addEdge(r, lx1, ly1, lx0, ly0);

		rx0 = rx1 = p1->x + p1->dmx * w;
		ry0 = ry1 = p1->y + p1->dmy * w;
		ansvg__addEdge(r, right->x, right->y, rx1, ry1);
	}

	left->x = lx1; left->y = ly1;
	right->x = rx1; right->y = ry1;
}

static void ansvg__roundJoin(ANSVGrasterizer* r, ANSVGpoint* left, ANSVGpoint* right, ANSVGpoint* p0, ANSVGpoint* p1, float lineWidth, int ncap)
{
	int i, n;
	float w = lineWidth * 0.5f;
	float dlx0 = p0->dy, dly0 = -p0->dx;
	float dlx1 = p1->dy, dly1 = -p1->dx;
	float a0 = atan2f(dly0, dlx0);
	float a1 = atan2f(dly1, dlx1);
	float da = a1 - a0;
	float lx, ly, rx, ry;

	if (da < ANSVG_PI) da += ANSVG_PI*2;
	if (da > ANSVG_PI) da -= ANSVG_PI*2;

	n = (int)ceilf((ansvg__absf(da) / ANSVG_PI) * (float)ncap);
	if (n < 2) n = 2;
	if (n > ncap) n = ncap;

	lx = left->x;
	ly = left->y;
	rx = right->x;
	ry = right->y;

	for (i = 0; i < n; i++) {
		float u = (float)i/(float)(n-1);
		float a = a0 + u*da;
		float ax = cosf(a) * w, ay = sinf(a) * w;
		float lx1 = p1->x - ax, ly1 = p1->y - ay;
		float rx1 = p1->x + ax, ry1 = p1->y + ay;

		ansvg__addEdge(r, lx1, ly1, lx, ly);
		ansvg__addEdge(r, rx, ry, rx1, ry1);

		lx = lx1; ly = ly1;
		rx = rx1; ry = ry1;
	}

	left->x = lx; left->y = ly;
	right->x = rx; right->y = ry;
}

static void ansvg__straightJoin(ANSVGrasterizer* r, ANSVGpoint* left, ANSVGpoint* right, ANSVGpoint* p1, float lineWidth)
{
	float w = lineWidth * 0.5f;
	float lx = p1->x - (p1->dmx * w), ly = p1->y - (p1->dmy * w);
	float rx = p1->x + (p1->dmx * w), ry = p1->y + (p1->dmy * w);

	ansvg__addEdge(r, lx, ly, left->x, left->y);
	ansvg__addEdge(r, right->x, right->y, rx, ry);

	left->x = lx; left->y = ly;
	right->x = rx; right->y = ry;
}

static int ansvg__curveDivs(float r, float arc, float tol)
{
	float da = acosf(r / (r + tol)) * 2.0f;
	int divs = (int)ceilf(arc / da);
	if (divs < 2) divs = 2;
	return divs;
}

static void ansvg__expandStroke(ANSVGrasterizer* r, ANSVGpoint* points, int npoints, int closed, int lineJoin, int lineCap, float lineWidth)
{
	int ncap = ansvg__curveDivs(lineWidth*0.5f, ANSVG_PI, r->tessTol);	// Calculate divisions per half circle.
	ANSVGpoint left = {0,0,0,0,0,0,0,0}, right = {0,0,0,0,0,0,0,0}, firstLeft = {0,0,0,0,0,0,0,0}, firstRight = {0,0,0,0,0,0,0,0};
	ANSVGpoint* p0, *p1;
	int j, s, e;

	// Build stroke edges
	if (closed) {
		// Looping
		p0 = &points[npoints-1];
		p1 = &points[0];
		s = 0;
		e = npoints;
	} else {
		// Add cap
		p0 = &points[0];
		p1 = &points[1];
		s = 1;
		e = npoints-1;
	}

	if (closed) {
		ansvg__initClosed(&left, &right, p0, p1, lineWidth);
		firstLeft = left;
		firstRight = right;
	} else {
		// Add cap
		float dx = p1->x - p0->x;
		float dy = p1->y - p0->y;
		ansvg__normalize(&dx, &dy);
		if (lineCap == NSVG_CAP_BUTT)
			ansvg__buttCap(r, &left, &right, p0, dx, dy, lineWidth, 0);
		else if (lineCap == NSVG_CAP_SQUARE)
			ansvg__squareCap(r, &left, &right, p0, dx, dy, lineWidth, 0);
		else if (lineCap == NSVG_CAP_ROUND)
			ansvg__roundCap(r, &left, &right, p0, dx, dy, lineWidth, ncap, 0);
	}

	for (j = s; j < e; ++j) {
		if (p1->flags & ANSVG_PT_CORNER) {
			if (lineJoin == NSVG_JOIN_ROUND)
				ansvg__roundJoin(r, &left, &right, p0, p1, lineWidth, ncap);
			else if (lineJoin == NSVG_JOIN_BEVEL || (p1->flags & ANSVG_PT_BEVEL))
				ansvg__bevelJoin(r, &left, &right, p0, p1, lineWidth);
			else
				ansvg__miterJoin(r, &left, &right, p0, p1, lineWidth);
		} else {
			ansvg__straightJoin(r, &left, &right, p1, lineWidth);
		}
		p0 = p1++;
	}

	if (closed) {
		// Loop it
		ansvg__addEdge(r, firstLeft.x, firstLeft.y, left.x, left.y);
		ansvg__addEdge(r, right.x, right.y, firstRight.x, firstRight.y);
	} else {
		// Add cap
		float dx = p1->x - p0->x;
		float dy = p1->y - p0->y;
		ansvg__normalize(&dx, &dy);
		if (lineCap == NSVG_CAP_BUTT)
			ansvg__buttCap(r, &right, &left, p1, -dx, -dy, lineWidth, 1);
		else if (lineCap == NSVG_CAP_SQUARE)
			ansvg__squareCap(r, &right, &left, p1, -dx, -dy, lineWidth, 1);
		else if (lineCap == NSVG_CAP_ROUND)
			ansvg__roundCap(r, &right, &left, p1, -dx, -dy, lineWidth, ncap, 1);
	}
}

static void ansvg__prepareStroke(ANSVGrasterizer* r, float miterLimit, int lineJoin)
{
	int i, j;
	ANSVGpoint* p0, *p1;

	p0 = &r->points[r->npoints-1];
	p1 = &r->points[0];
	for (i = 0; i < r->npoints; i++) {
		// Calculate segment direction and length
		p0->dx = p1->x - p0->x;
		p0->dy = p1->y - p0->y;
		p0->len = ansvg__normalize(&p0->dx, &p0->dy);
		// Advance
		p0 = p1++;
	}

	// calculate joins
	p0 = &r->points[r->npoints-1];
	p1 = &r->points[0];
	for (j = 0; j < r->npoints; j++) {
		float dlx0, dly0, dlx1, dly1, dmr2, cross;
		dlx0 = p0->dy;
		dly0 = -p0->dx;
		dlx1 = p1->dy;
		dly1 = -p1->dx;
		// Calculate extrusions
		p1->dmx = (dlx0 + dlx1) * 0.5f;
		p1->dmy = (dly0 + dly1) * 0.5f;
		dmr2 = p1->dmx*p1->dmx + p1->dmy*p1->dmy;
		if (dmr2 > 0.000001f) {
			float s2 = 1.0f / dmr2;
			if (s2 > 600.0f) {
				s2 = 600.0f;
			}
			p1->dmx *= s2;
			p1->dmy *= s2;
		}

		// Clear flags, but keep the corner.
		p1->flags = (p1->flags & ANSVG_PT_CORNER) ? ANSVG_PT_CORNER : 0;

		// Keep track of left turns.
		cross = p1->dx * p0->dy - p0->dx * p1->dy;
		if (cross > 0.0f)
			p1->flags |= ANSVG_PT_LEFT;

		// Check to see if the corner needs to be beveled.
		if (p1->flags & ANSVG_PT_CORNER) {
			if ((dmr2 * miterLimit*miterLimit) < 1.0f || lineJoin == NSVG_JOIN_BEVEL || lineJoin == NSVG_JOIN_ROUND) {
				p1->flags |= ANSVG_PT_BEVEL;
			}
		}

		p0 = p1++;
	}
}

static void ansvg__flattenShapeStroke(ANSVGrasterizer* r, NSVGshape* shape, float scale)
{
	int i, j, closed;
	NSVGpath* path;
	ANSVGpoint* p0, *p1;
	float miterLimit = shape->miterLimit;
	int lineJoin = shape->strokeLineJoin;
	int lineCap = shape->strokeLineCap;
	float lineWidth = shape->strokeWidth * scale;

	for (path = shape->paths; path != NULL; path = path->next) {
		// Flatten path
		r->npoints = 0;
		ansvg__addPathPoint(r, path->pts[0]*scale, path->pts[1]*scale, ANSVG_PT_CORNER);
		for (i = 0; i < path->npts-1; i += 3) {
			float* p = &path->pts[i*2];
			ansvg__flattenCubicBez(r, p[0]*scale,p[1]*scale, p[2]*scale,p[3]*scale, p[4]*scale,p[5]*scale, p[6]*scale,p[7]*scale, 0, ANSVG_PT_CORNER);
		}
		if (r->npoints < 2)
			continue;

		closed = path->closed;

		// If the first and last points are the same, remove the last, mark as closed path.
		p0 = &r->points[r->npoints-1];
		p1 = &r->points[0];
		if (ansvg__ptEquals(p0->x,p0->y, p1->x,p1->y, r->distTol)) {
			r->npoints--;
			p0 = &r->points[r->npoints-1];
			closed = 1;
		}

		if (shape->strokeDashCount > 0) {
			int idash = 0, dashState = 1;
			float totalDist = 0, dashLen, allDashLen, dashOffset;
			ANSVGpoint cur;

			if (closed)
				ansvg__appendPathPoint(r, r->points[0]);

			// Duplicate points -> points2.
			ansvg__duplicatePoints(r);

			r->npoints = 0;
 			cur = r->points2[0];
			ansvg__appendPathPoint(r, cur);

			// Figure out dash offset.
			allDashLen = 0;
			for (j = 0; j < shape->strokeDashCount; j++)
				allDashLen += shape->strokeDashArray[j];
			if (shape->strokeDashCount & 1)
				allDashLen *= 2.0f;
			// Find location inside pattern
			dashOffset = fmodf(shape->strokeDashOffset, allDashLen);
			if (dashOffset < 0.0f)
				dashOffset += allDashLen;

			while (dashOffset > shape->strokeDashArray[idash]) {
				dashOffset -= shape->strokeDashArray[idash];
				idash = (idash + 1) % shape->strokeDashCount;
			}
			dashLen = (shape->strokeDashArray[idash] - dashOffset) * scale;

			for (j = 1; j < r->npoints2; ) {
				float dx = r->points2[j].x - cur.x;
				float dy = r->points2[j].y - cur.y;
				float dist = sqrtf(dx*dx + dy*dy);

				if ((totalDist + dist) > dashLen) {
					// Calculate intermediate point
					float d = (dashLen - totalDist) / dist;
					float x = cur.x + dx * d;
					float y = cur.y + dy * d;
					ansvg__addPathPoint(r, x, y, ANSVG_PT_CORNER);

					// Stroke
					if (r->npoints > 1 && dashState) {
						ansvg__prepareStroke(r, miterLimit, lineJoin);
						ansvg__expandStroke(r, r->points, r->npoints, 0, lineJoin, lineCap, lineWidth);
					}
					// Advance dash pattern
					dashState = !dashState;
					idash = (idash+1) % shape->strokeDashCount;
					dashLen = shape->strokeDashArray[idash] * scale;
					// Restart
					cur.x = x;
					cur.y = y;
					cur.flags = ANSVG_PT_CORNER;
					totalDist = 0.0f;
					r->npoints = 0;
					ansvg__appendPathPoint(r, cur);
				} else {
					totalDist += dist;
					cur = r->points2[j];
					ansvg__appendPathPoint(r, cur);
					j++;
				}
			}
			// Stroke any leftover path
			if (r->npoints > 1 && dashState) {
				ansvg__prepareStroke(r, miterLimit, lineJoin);
				ansvg__expandStroke(r, r->points, r->npoints, 0, lineJoin, lineCap, lineWidth);
			}
		} else {
			ansvg__prepareStroke(r, miterLimit, lineJoin);
			ansvg__expandStroke(r, r->points, r->npoints, closed, lineJoin, lineCap, lineWidth);
		}
	}
}

static int ansvg__cmpEdge(const void *p, const void *q)
{
	const ANSVGedge* a = (const ANSVGedge*)p;
	const ANSVGedge* b = (const ANSVGedge*)q;

	if (a->y0 < b->y0) return -1;
	if (a->y0 > b->y0) return  1;
	return 0;
}


static ANSVGactiveEdge* ansvg__addActive(ANSVGrasterizer* r, ANSVGedge* e, float startPoint)
{
	 ANSVGactiveEdge* z;

	if (r->freelist != NULL) {
		// Restore from freelist.
		z = r->freelist;
		r->freelist = z->next;
	} else {
		// Alloc new edge.
		z = (ANSVGactiveEdge*)ansvg__alloc(r, sizeof(ANSVGactiveEdge));
		if (z == NULL) return NULL;
	}

	float dxdy = (e->x1 - e->x0) / (e->y1 - e->y0);
//	STBTT_assert(e->y0 <= start_point);
	// round dx down to avoid going too far
	if (dxdy < 0)
		z->dx = (int)(-ansvg__roundf(ANSVG__FIX * -dxdy));
	else
		z->dx = (int)ansvg__roundf(ANSVG__FIX * dxdy);
	z->x = (int)ansvg__roundf(ANSVG__FIX * (e->x0 + dxdy * (startPoint - e->y0)));
//	z->x -= off_x * FIX;
	z->ey = e->y1;
	z->next = 0;
	z->dir = e->dir;

	return z;
}

static void ansvg__freeActive(ANSVGrasterizer* r, ANSVGactiveEdge* z)
{
	z->next = r->freelist;
	r->freelist = z;
}

static void ansvg__fillScanline(unsigned char* scanline, int len, int x0, int x1, int maxWeight, int* xmin, int* xmax)
{
	int i = x0 >> ANSVG__FIXSHIFT;
	int j = x1 >> ANSVG__FIXSHIFT;
	if (i < *xmin) *xmin = i;
	if (j > *xmax) *xmax = j;
	if (i < len && j >= 0) {
		if (i == j) {
			// x0,x1 are the same pixel, so compute combined coverage
			scanline[i] = (unsigned char)(scanline[i] + ((x1 - x0) * maxWeight >> ANSVG__FIXSHIFT));
		} else {
			if (i >= 0) // add antialiasing for x0
				scanline[i] = (unsigned char)(scanline[i] + (((ANSVG__FIX - (x0 & ANSVG__FIXMASK)) * maxWeight) >> ANSVG__FIXSHIFT));
			else
				i = -1; // clip

			if (j < len) // add antialiasing for x1
				scanline[j] = (unsigned char)(scanline[j] + (((x1 & ANSVG__FIXMASK) * maxWeight) >> ANSVG__FIXSHIFT));
			else
				j = len; // clip

			for (++i; i < j; ++i) // fill pixels between x0 and x1
				scanline[i] = (unsigned char)(scanline[i] + maxWeight);
		}
	}
}

// note: this routine clips fills that extend off the edges... ideally this
// wouldn't happen, but it could happen if the truetype glyph bounding boxes
// are wrong, or if the user supplies a too-small bitmap
static void ansvg__fillActiveEdges(unsigned char* scanline, int len, ANSVGactiveEdge* e, int maxWeight, int* xmin, int* xmax, char fillRule)
{
	// non-zero winding fill
	int x0 = 0, w = 0;

	if (fillRule == NSVG_FILLRULE_NONZERO) {
		// Non-zero
		while (e != NULL) {
			if (w == 0) {
				// if we're currently at zero, we need to record the edge start point
				x0 = e->x; w += e->dir;
			} else {
				int x1 = e->x; w += e->dir;
				// if we went to zero, we need to draw
				if (w == 0)
					ansvg__fillScanline(scanline, len, x0, x1, maxWeight, xmin, xmax);
			}
			e = e->next;
		}
	} else if (fillRule == NSVG_FILLRULE_EVENODD) {
		// Even-odd
		while (e != NULL) {
			if (w == 0) {
				// if we're currently at zero, we need to record the edge start point
				x0 = e->x; w = 1;
			} else {
				int x1 = e->x; w = 0;
				ansvg__fillScanline(scanline, len, x0, x1, maxWeight, xmin, xmax);
			}
			e = e->next;
		}
	}
}

static float ansvg__clampf(float a, float mn, float mx) {
	if (isnan(a))
		return mn;
	return a < mn ? mn : (a > mx ? mx : a);
}

static unsigned int ansvg__RGBA(unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
	return ((unsigned int)r) | ((unsigned int)g << 8) | ((unsigned int)b << 16) | ((unsigned int)a << 24);
}

static unsigned int ansvg__lerpRGBA(unsigned int c0, unsigned int c1, float u)
{
	int iu = (int)(ansvg__clampf(u, 0.0f, 1.0f) * 256.0f);
	int r = (((c0) & 0xff)*(256-iu) + (((c1) & 0xff)*iu)) >> 8;
	int g = (((c0>>8) & 0xff)*(256-iu) + (((c1>>8) & 0xff)*iu)) >> 8;
	int b = (((c0>>16) & 0xff)*(256-iu) + (((c1>>16) & 0xff)*iu)) >> 8;
	int a = (((c0>>24) & 0xff)*(256-iu) + (((c1>>24) & 0xff)*iu)) >> 8;
	return ansvg__RGBA((unsigned char)r, (unsigned char)g, (unsigned char)b, (unsigned char)a);
}

static unsigned int ansvg__applyOpacity(unsigned int c, float u)
{
	int iu = (int)(ansvg__clampf(u, 0.0f, 1.0f) * 256.0f);
	int r = (c) & 0xff;
	int g = (c>>8) & 0xff;
	int b = (c>>16) & 0xff;
	int a = (((c>>24) & 0xff)*iu) >> 8;
	return ansvg__RGBA((unsigned char)r, (unsigned char)g, (unsigned char)b, (unsigned char)a);
}

static inline int ansvg__div255(int x)
{
    return ((x+1) * 257) >> 16;
}

static inline void ansvg__writePixel(unsigned char *dst, int cr, int cg, int cb, int a,
	enum ANSVGrenderMode renderMode)
{
	int r, g, b;
	int ia = 255 - a;

	// Premultiply
	r = ansvg__div255(cr * a);
	g = ansvg__div255(cg * a);
	b = ansvg__div255(cb * a);

	if (renderMode == ANSVG_RENDER_ALPHA) {
		// int lum = (77 * r + 150 * g + 29 * b + 128) >> 8;
		int alpha = a;
		alpha += ansvg__div255(ia * (int)dst[0]);
		dst[0] = alpha;
	} else {
		// Blend over
		r += ansvg__div255(ia * (int)dst[0]);
		g += ansvg__div255(ia * (int)dst[1]);
		b += ansvg__div255(ia * (int)dst[2]);
		a += ansvg__div255(ia * (int)dst[3]);

		if (renderMode == ANSVG_RENDER_RGBA) {
			dst[0] = (unsigned char)r;
			dst[1] = (unsigned char)g;
			dst[2] = (unsigned char)b;
			dst[3] = (unsigned char)a;
		} else {
			dst[0] = (unsigned char)b;
			dst[1] = (unsigned char)g;
			dst[2] = (unsigned char)r;
			dst[3] = (unsigned char)a;
		}
	}
}

static void ansvg__scanlineSolid(unsigned char* dst, int count, unsigned char* cover, int x, int y,
								float tx, float ty, float scale, ANSVGcachedPaint* cache,
								enum ANSVGrenderMode renderMode)
{
	int advance = renderMode == ANSVG_RENDER_ALPHA ? 1 : 4;
	if (cache->type == NSVG_PAINT_COLOR) {
		int i, cr, cg, cb, ca;
		cr = cache->colors[0] & 0xff;
		cg = (cache->colors[0] >> 8) & 0xff;
		cb = (cache->colors[0] >> 16) & 0xff;
		ca = (cache->colors[0] >> 24) & 0xff;

		for (i = 0; i < count; i++) {
			int a = ansvg__div255((int)cover[0] * ca);
			ansvg__writePixel(dst, cr, cg, cb, a, renderMode);
			cover++;
			dst += advance;
		}
	} else if (cache->type == NSVG_PAINT_LINEAR_GRADIENT) {
		// TODO: spread modes.
		// TODO: plenty of opportunities to optimize.
		float fx, fy, dx, gy;
		float* t = cache->xform;
		int i, cr, cg, cb, ca;
		unsigned int c;

		fx = ((float)x - tx) / scale;
		fy = ((float)y - ty) / scale;
		dx = 1.0f / scale;

		for (i = 0; i < count; i++) {
			int r,g,b,a,ia;
			gy = fx*t[1] + fy*t[3] + t[5];
			c = cache->colors[(int)ansvg__clampf(gy*255.0f, 0, 255.0f)];
			cr = (c) & 0xff;
			cg = (c >> 8) & 0xff;
			cb = (c >> 16) & 0xff;
			ca = (c >> 24) & 0xff;

			a = ansvg__div255((int)cover[0] * ca);
			ansvg__writePixel(dst, cr, cg, cb, a, renderMode);

			cover++;
			dst += advance;
			fx += dx;
		}
	} else if (cache->type == NSVG_PAINT_RADIAL_GRADIENT) {
		// TODO: spread modes.
		// TODO: plenty of opportunities to optimize.
		// TODO: focus (fx,fy)
		float fx, fy, dx, gx, gy, gd;
		float* t = cache->xform;
		int i, cr, cg, cb, ca;
		unsigned int c;

		fx = ((float)x - tx) / scale;
		fy = ((float)y - ty) / scale;
		dx = 1.0f / scale;

		for (i = 0; i < count; i++) {
			int r,g,b,a,ia;
			gx = fx*t[0] + fy*t[2] + t[4];
			gy = fx*t[1] + fy*t[3] + t[5];
			gd = sqrtf(gx*gx + gy*gy);
			c = cache->colors[(int)ansvg__clampf(gd*255.0f, 0, 255.0f)];
			cr = (c) & 0xff;
			cg = (c >> 8) & 0xff;
			cb = (c >> 16) & 0xff;
			ca = (c >> 24) & 0xff;

			a = ansvg__div255((int)cover[0] * ca);
			ansvg__writePixel(dst, cr, cg, cb, a, renderMode);

			cover++;
			dst += advance;
			fx += dx;
		}
	}
}

static void ansvg__rasterizeSortedEdges(ANSVGrasterizer *r, float tx, float ty, float scale, ANSVGcachedPaint* cache, char fillRule, enum ANSVGrenderMode renderMode)
{
	ANSVGactiveEdge *active = NULL;
	int y, s;
	int e = 0;
	int maxWeight = (255 / ANSVG__SUBSAMPLES);  // weight per vertical scanline
	int xmin, xmax;

	for (y = 0; y < r->height; y++) {
		memset(r->scanline, 0, r->width);
		xmin = r->width;
		xmax = 0;
		for (s = 0; s < ANSVG__SUBSAMPLES; ++s) {
			// find center of pixel for this scanline
			float scany = (float)(y*ANSVG__SUBSAMPLES + s) + 0.5f;
			ANSVGactiveEdge **step = &active;

			// update all active edges;
			// remove all active edges that terminate before the center of this scanline
			while (*step) {
				ANSVGactiveEdge *z = *step;
				if (z->ey <= scany) {
					*step = z->next; // delete from list
//					NSVG__assert(z->valid);
					ansvg__freeActive(r, z);
				} else {
					z->x += z->dx; // advance to position for current scanline
					step = &((*step)->next); // advance through list
				}
			}

			// resort the list if needed
			for (;;) {
				int changed = 0;
				step = &active;
				while (*step && (*step)->next) {
					if ((*step)->x > (*step)->next->x) {
						ANSVGactiveEdge* t = *step;
						ANSVGactiveEdge* q = t->next;
						t->next = q->next;
						q->next = t;
						*step = q;
						changed = 1;
					}
					step = &(*step)->next;
				}
				if (!changed) break;
			}

			// insert all edges that start before the center of this scanline -- omit ones that also end on this scanline
			while (e < r->nedges && r->edges[e].y0 <= scany) {
				if (r->edges[e].y1 > scany) {
					ANSVGactiveEdge* z = ansvg__addActive(r, &r->edges[e], scany);
					if (z == NULL) break;
					// find insertion point
					if (active == NULL) {
						active = z;
					} else if (z->x < active->x) {
						// insert at front
						z->next = active;
						active = z;
					} else {
						// find thing to insert AFTER
						ANSVGactiveEdge* p = active;
						while (p->next && p->next->x < z->x)
							p = p->next;
						// at this point, p->next->x is NOT < z->x
						z->next = p->next;
						p->next = z;
					}
				}
				e++;
			}

			// now process all active edges in non-zero fashion
			if (active != NULL)
				ansvg__fillActiveEdges(r->scanline, r->width, active, maxWeight, &xmin, &xmax, fillRule);
		}
		// Blit
		if (xmin < 0) xmin = 0;
		if (xmax > r->width-1) xmax = r->width-1;
		if (xmin <= xmax) {
			int adv = renderMode == ANSVG_RENDER_ALPHA ? 1 : 4;
			ansvg__scanlineSolid(&r->bitmap[y * r->stride] + xmin*adv, xmax-xmin+1, &r->scanline[xmin], xmin, y, tx,ty, scale, cache, renderMode);
		}
	}

}

static void ansvg__unpremultiplyAlpha(unsigned char* image, int w, int h, int stride)
{
	int x,y;

	// Unpremultiply
	for (y = 0; y < h; y++) {
		unsigned char *row = &image[y*stride];
		for (x = 0; x < w; x++) {
			int r = row[0], g = row[1], b = row[2], a = row[3];
			if (a != 0) {
				row[0] = (unsigned char)(r*255/a);
				row[1] = (unsigned char)(g*255/a);
				row[2] = (unsigned char)(b*255/a);
			}
			row += 4;
		}
	}

	// Defringe
	for (y = 0; y < h; y++) {
		unsigned char *row = &image[y*stride];
		for (x = 0; x < w; x++) {
			int r = 0, g = 0, b = 0, a = row[3], n = 0;
			if (a == 0) {
				if (x-1 > 0 && row[-1] != 0) {
					r += row[-4];
					g += row[-3];
					b += row[-2];
					n++;
				}
				if (x+1 < w && row[7] != 0) {
					r += row[4];
					g += row[5];
					b += row[6];
					n++;
				}
				if (y-1 > 0 && row[-stride+3] != 0) {
					r += row[-stride];
					g += row[-stride+1];
					b += row[-stride+2];
					n++;
				}
				if (y+1 < h && row[stride+3] != 0) {
					r += row[stride];
					g += row[stride+1];
					b += row[stride+2];
					n++;
				}
				if (n > 0) {
					row[0] = (unsigned char)(r/n);
					row[1] = (unsigned char)(g/n);
					row[2] = (unsigned char)(b/n);
				}
			}
			row += 4;
		}
	}
}


static void ansvg__initPaint(ANSVGcachedPaint* cache, NSVGpaint* paint, float opacity)
{
	int i, j;
	NSVGgradient* grad;

	cache->type = paint->type;

	if (paint->type == NSVG_PAINT_COLOR) {
		cache->colors[0] = ansvg__applyOpacity(paint->color, opacity);
		return;
	}

	grad = paint->gradient;

	cache->spread = grad->spread;
	memcpy(cache->xform, grad->xform, sizeof(float)*6);

	if (grad->nstops == 0) {
		for (i = 0; i < 256; i++)
			cache->colors[i] = 0;
	} else if (grad->nstops == 1) {
		unsigned int color = ansvg__applyOpacity(grad->stops[0].color, opacity);
		for (i = 0; i < 256; i++)
			cache->colors[i] = color;
	} else {
		unsigned int ca, cb = 0;
		float ua, ub, du, u;
		int ia, ib, count;

		ca = ansvg__applyOpacity(grad->stops[0].color, opacity);
		ua = ansvg__clampf(grad->stops[0].offset, 0, 1);
		ub = ansvg__clampf(grad->stops[grad->nstops-1].offset, ua, 1);
		ia = (int)(ua * 255.0f);
		ib = (int)(ub * 255.0f);
		for (i = 0; i < ia; i++) {
			cache->colors[i] = ca;
		}

		for (i = 0; i < grad->nstops-1; i++) {
			ca = ansvg__applyOpacity(grad->stops[i].color, opacity);
			cb = ansvg__applyOpacity(grad->stops[i+1].color, opacity);
			ua = ansvg__clampf(grad->stops[i].offset, 0, 1);
			ub = ansvg__clampf(grad->stops[i+1].offset, 0, 1);
			ia = (int)(ua * 255.0f);
			ib = (int)(ub * 255.0f);
			count = ib - ia;
			if (count <= 0) continue;
			u = 0;
			du = 1.0f / (float)count;
			for (j = 0; j < count; j++) {
				cache->colors[ia+j] = ansvg__lerpRGBA(ca,cb,u);
				u += du;
			}
		}

		for (i = ib; i < 256; i++)
			cache->colors[i] = cb;
	}

}

/*
static void dumpEdges(ANSVGrasterizer* r, const char* name)
{
	float xmin = 0, xmax = 0, ymin = 0, ymax = 0;
	ANSVGedge *e = NULL;
	int i;
	if (r->nedges == 0) return;
	FILE* fp = fopen(name, "w");
	if (fp == NULL) return;

	xmin = xmax = r->edges[0].x0;
	ymin = ymax = r->edges[0].y0;
	for (i = 0; i < r->nedges; i++) {
		e = &r->edges[i];
		xmin = nsvg__minf(xmin, e->x0);
		xmin = nsvg__minf(xmin, e->x1);
		xmax = nsvg__maxf(xmax, e->x0);
		xmax = nsvg__maxf(xmax, e->x1);
		ymin = nsvg__minf(ymin, e->y0);
		ymin = nsvg__minf(ymin, e->y1);
		ymax = nsvg__maxf(ymax, e->y0);
		ymax = nsvg__maxf(ymax, e->y1);
	}

	fprintf(fp, "<svg viewBox=\"%f %f %f %f\" xmlns=\"http://www.w3.org/2000/svg\">", xmin, ymin, (xmax - xmin), (ymax - ymin));

	for (i = 0; i < r->nedges; i++) {
		e = &r->edges[i];
		fprintf(fp ,"<line x1=\"%f\" y1=\"%f\" x2=\"%f\" y2=\"%f\" style=\"stroke:#000;\" />", e->x0,e->y0, e->x1,e->y1);
	}

	for (i = 0; i < r->npoints; i++) {
		if (i+1 < r->npoints)
			fprintf(fp ,"<line x1=\"%f\" y1=\"%f\" x2=\"%f\" y2=\"%f\" style=\"stroke:#f00;\" />", r->points[i].x, r->points[i].y, r->points[i+1].x, r->points[i+1].y);
		fprintf(fp ,"<circle cx=\"%f\" cy=\"%f\" r=\"1\" style=\"fill:%s;\" />", r->points[i].x, r->points[i].y, r->points[i].flags == 0 ? "#f00" : "#0f0");
	}

	fprintf(fp, "</svg>");
	fclose(fp);
}
*/

void ansvgRasterize(ANSVGrasterizer* r,
				   NSVGimage* image, float tx, float ty, float scale,
				   unsigned char* dst, int w, int h, int stride, enum ANSVGrenderMode renderMode)
{
	NSVGshape *shape = NULL;
	ANSVGedge *e = NULL;
	ANSVGcachedPaint cache;
	int i;
    int j;
    unsigned char paintOrder;

    int pix_width = renderMode == ANSVG_RENDER_ALPHA ? 1 : 4;

	r->bitmap = dst;
	r->width = w;
	r->height = h;
	r->stride = stride;

	if (w > r->cscanline) {
		r->cscanline = w;
		r->scanline = (unsigned char*)realloc(r->scanline, w);
		if (r->scanline == NULL) return;
	}

	for (i = 0; i < h; i++)
		memset(&dst[i*stride], 0, w*pix_width);

	for (shape = image->shapes; shape != NULL; shape = shape->next) {
		if (!(shape->flags & NSVG_FLAGS_VISIBLE))
			continue;

        for (j = 0; j < 3; j++) {
            paintOrder = (shape->paintOrder >> (2 * j)) & 0x03;

            if (paintOrder == NSVG_PAINT_FILL && shape->fill.type != NSVG_PAINT_NONE) {
                ansvg__resetPool(r);
                r->freelist = NULL;
                r->nedges = 0;

                ansvg__flattenShape(r, shape, scale);

                // Scale and translate edges
                for (i = 0; i < r->nedges; i++) {
                    e = &r->edges[i];
                    e->x0 = tx + e->x0;
                    e->y0 = (ty + e->y0) * ANSVG__SUBSAMPLES;
                    e->x1 = tx + e->x1;
                    e->y1 = (ty + e->y1) * ANSVG__SUBSAMPLES;
                }

                // Rasterize edges
                if (r->nedges != 0)
                    qsort(r->edges, r->nedges, sizeof(ANSVGedge), ansvg__cmpEdge);

                // now, traverse the scanlines and find the intersections on each scanline, use non-zero rule
                ansvg__initPaint(&cache, &shape->fill, shape->opacity);

                ansvg__rasterizeSortedEdges(r, tx,ty,scale, &cache, shape->fillRule, renderMode);
            }
            if (paintOrder == NSVG_PAINT_STROKE && shape->stroke.type != NSVG_PAINT_NONE && (shape->strokeWidth * scale) > 0.01f) {
                ansvg__resetPool(r);
                r->freelist = NULL;
                r->nedges = 0;

                ansvg__flattenShapeStroke(r, shape, scale);

    //			dumpEdges(r, "edge.svg");

                // Scale and translate edges
                for (i = 0; i < r->nedges; i++) {
                    e = &r->edges[i];
                    e->x0 = tx + e->x0;
                    e->y0 = (ty + e->y0) * ANSVG__SUBSAMPLES;
                    e->x1 = tx + e->x1;
                    e->y1 = (ty + e->y1) * ANSVG__SUBSAMPLES;
                }

                // Rasterize edges
                if (r->nedges != 0)
                    qsort(r->edges, r->nedges, sizeof(ANSVGedge), ansvg__cmpEdge);

                // now, traverse the scanlines and find the intersections on each scanline, use non-zero rule
                ansvg__initPaint(&cache, &shape->stroke, shape->opacity);

                ansvg__rasterizeSortedEdges(r, tx,ty,scale, &cache, NSVG_FILLRULE_NONZERO, renderMode);
            }
        }
	}

	if (renderMode != ANSVG_RENDER_ALPHA)
		ansvg__unpremultiplyAlpha(dst, w, h, stride);

	r->bitmap = NULL;
	r->width = 0;
	r->height = 0;
	r->stride = 0;
}

#endif // AU_NANOSVGRAST_IMPLEMENTATION

#endif // AU_NANOSVGRAST_H
