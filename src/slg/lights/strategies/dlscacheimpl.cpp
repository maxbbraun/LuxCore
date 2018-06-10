/***************************************************************************
 * Copyright 1998-2018 by authors (see AUTHORS.txt)                        *
 *                                                                         *
 *   This file is part of LuxCoreRender.                                   *
 *                                                                         *
 * Licensed under the Apache License, Version 2.0 (the "License");         *
 * you may not use this file except in compliance with the License.        *
 * You may obtain a copy of the License at                                 *
 *                                                                         *
 *     http://www.apache.org/licenses/LICENSE-2.0                          *
 *                                                                         *
 * Unless required by applicable law or agreed to in writing, software     *
 * distributed under the License is distributed on an "AS IS" BASIS,       *
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.*
 * See the License for the specific language governing permissions and     *
 * limitations under the License.                                          *
 ***************************************************************************/

#include "luxrays/core/geometry/bbox.h"
#include "slg/samplers/sobol.h"
#include "slg/lights/strategies/dlscacheimpl.h"

using namespace std;
using namespace luxrays;
using namespace slg;

//------------------------------------------------------------------------------
// DLSCacheEntry
//------------------------------------------------------------------------------

namespace slg {

class DLSCacheEntry {
public:
	DLSCacheEntry(const Point &pnt, const Normal &nml) : p(pnt), n(nml) {
	}
	~DLSCacheEntry() { }
	
	Point p;
	Normal n;
};

}

//------------------------------------------------------------------------------
// DLSCOctree
//------------------------------------------------------------------------------

namespace slg {

class DLSCOctree {
public:
	DLSCOctree(const BBox &bbox, const float r, const float normAngle, const u_int md = 24) :
		worldBBox(bbox), maxDepth(md), entryRadius(r), entryRadius2(r * r),
		entryNormalCosAngle(cosf(Radians(normAngle))) {
		worldBBox.Expand(MachineEpsilon::E(worldBBox));
	}
	~DLSCOctree() {
		for (auto entry : allEntries)
			delete entry;
	}

	void Add(DLSCacheEntry *cacheEntry) {
		allEntries.push_back(cacheEntry);
		
		const Vector entryRadiusVector(entryRadius, entryRadius, entryRadius);
		const BBox entryBBox(cacheEntry->p - entryRadiusVector, cacheEntry->p + entryRadiusVector);

		AddImpl(&root, worldBBox, cacheEntry, entryBBox, DistanceSquared(entryBBox.pMin,  entryBBox.pMax));
	}

	const vector<DLSCacheEntry *> &GetAllEntries() const {
		return allEntries;
	}

	const DLSCacheEntry *GetEntry(const Point &p, const Normal &n) const {
		return GetEntryImpl(&root, worldBBox, p, n);
	}

	void DebugExport(const string &fileName, const float sphereRadius) const {
		Properties prop;

		prop <<
				Property("scene.materials.octree_material.type")("matte") <<
				Property("scene.materials.octree_material.kd")("0.75 0.75 0.75");

		for (u_int i = 0; i < allEntries.size(); ++i) {
			prop <<
				Property("scene.objects.octree_entry_" + ToString(i) + ".material")("octree_material") <<
				Property("scene.objects.octree_entry_" + ToString(i) + ".ply")("scenes/simple/sphere.ply") <<
				Property("scene.objects.octree_entry_" + ToString(i) + ".transformation")(Matrix4x4(
					sphereRadius, 0.f, 0.f, allEntries[i]->p.x,
					0.f, sphereRadius, 0.f, allEntries[i]->p.y,
					0.f, 0.f, sphereRadius, allEntries[i]->p.z,
					0.f, 0.f, 0.f, 1.f));
		}

		prop.Save(fileName);
	}
	
private:
	class DLSCOctreeNode {
	public:
		DLSCOctreeNode() {
			for (u_int i = 0; i < 8; ++i)
				children[i] = NULL;
		}

		~DLSCOctreeNode() {
			for (u_int i = 0; i < 8; ++i)
				delete children[i];
		}

		DLSCOctreeNode *children[8];
		vector<DLSCacheEntry *> entries;
	};

	BBox ChildNodeBBox(u_int child, const BBox &nodeBBox,
		const Point &pMid) const {
		BBox childBound;

		childBound.pMin.x = (child & 0x4) ? pMid.x : nodeBBox.pMin.x;
		childBound.pMax.x = (child & 0x4) ? nodeBBox.pMax.x : pMid.x;
		childBound.pMin.y = (child & 0x2) ? pMid.y : nodeBBox.pMin.y;
		childBound.pMax.y = (child & 0x2) ? nodeBBox.pMax.y : pMid.y;
		childBound.pMin.z = (child & 0x1) ? pMid.z : nodeBBox.pMin.z;
		childBound.pMax.z = (child & 0x1) ? nodeBBox.pMax.z : pMid.z;

		return childBound;
	}

	void AddImpl(DLSCOctreeNode *node, const BBox &nodeBBox,
		DLSCacheEntry *entry, const BBox &entryBBox,
		const float entryBBoxDiagonal2, const u_int depth = 0) {
		// Check if I have to store the entry in this node
		if ((depth == maxDepth) ||
				DistanceSquared(nodeBBox.pMin, nodeBBox.pMax) < entryBBoxDiagonal2) {
			node->entries.push_back(entry);
			return;
		}

		// Determine which children the item overlaps
		const Point pMid = .5 * (nodeBBox.pMin + nodeBBox.pMax);

		const bool x[2] = {
			entryBBox.pMin.x <= pMid.x,
			entryBBox.pMax.x > pMid.x
		};
		const bool y[2] = {
			entryBBox.pMin.y <= pMid.y,
			entryBBox.pMax.y > pMid.y
		};
		const bool z[2] = {
			entryBBox.pMin.z <= pMid.z,
			entryBBox.pMax.z > pMid.z
		};

		const bool overlap[8] = {
			bool(x[0] & y[0] & z[0]),
			bool(x[0] & y[0] & z[1]),
			bool(x[0] & y[1] & z[0]),
			bool(x[0] & y[1] & z[1]),
			bool(x[1] & y[0] & z[0]),
			bool(x[1] & y[0] & z[1]),
			bool(x[1] & y[1] & z[0]),
			bool(x[1] & y[1] & z[1])
		};

		for (u_int child = 0; child < 8; ++child) {
			if (!overlap[child])
				continue;

			// Allocated the child node if required
			if (!node->children[child])
				node->children[child] = new DLSCOctreeNode();

			// Add the entry to each overlapping child
			const BBox childBBox = ChildNodeBBox(child, nodeBBox, pMid);
			AddImpl(node->children[child], childBBox,
					entry, entryBBox, entryBBoxDiagonal2, depth + 1);
		}
	}

	const DLSCacheEntry *GetEntryImpl(const DLSCOctreeNode *node, const BBox &nodeBBox,
		const Point &p, const Normal &n) const {
		// Check if I'm inside the node bounding box
		if (!nodeBBox.Inside(p))
			return NULL;

		// Check every entry in this node
		for (auto entry : node->entries) {
			if ((DistanceSquared(p, entry->p) <= entryRadius2) &&
					(Dot(n, entry->n) >= entryNormalCosAngle)) {
				// I have found a valid entry
				return entry;
			}
		}
		
		// Check the children too
		const Point pMid = .5 * (nodeBBox.pMin + nodeBBox.pMax);
		for (u_int child = 0; child < 8; ++child) {
			if (node->children[child]) {
				const BBox childBBox = ChildNodeBBox(child, nodeBBox, pMid);

				const DLSCacheEntry *entry = GetEntryImpl(node->children[child], childBBox,
						p, n);
				if (entry) {
					// I have found a valid entry
					return entry;
				}
			}
		}
		
		return NULL;
	}

	BBox worldBBox;
	
	u_int maxDepth;
	float entryRadius, entryRadius2, entryNormalCosAngle;

	DLSCOctreeNode root;
	vector<DLSCacheEntry *> allEntries;
};

}

//------------------------------------------------------------------------------
// Direct light sampling cache
//------------------------------------------------------------------------------

DirectLightSamplingCache::DirectLightSamplingCache() {
	maxDepth = 4;
	sampleCount = 1000000;
	entryRadius = .25f;
	entryNormalAngle = 10.f;

	octree = NULL;
}

DirectLightSamplingCache::~DirectLightSamplingCache() {
	delete octree;
}

void DirectLightSamplingCache::GenerateEyeRay(const Camera *camera, Ray &eyeRay,
		PathVolumeInfo &volInfo, Sampler *sampler, SampleResult &sampleResult) const {
	const u_int *subRegion = camera->filmSubRegion;
	sampleResult.filmX = subRegion[0] + sampler->GetSample(0) * (subRegion[1] - subRegion[0] + 1);
	sampleResult.filmY = subRegion[2] + sampler->GetSample(1) * (subRegion[3] - subRegion[2] + 1);

	camera->GenerateRay(sampleResult.filmX, sampleResult.filmY, &eyeRay, &volInfo,
		sampler->GetSample(2), sampler->GetSample(3), sampler->GetSample(4));
}

void DirectLightSamplingCache::Build(const Scene *scene) {
	SLG_LOG("Building direct light sampling cache");

	// Initialize the Octree where to store the cache points
	delete octree;
	octree = new DLSCOctree(scene->dataSet->GetBBox(), entryRadius, entryNormalAngle);
			
	// Initialize the sampler
	RandomGenerator rnd(131);
	SobolSamplerSharedData sharedData(&rnd, NULL);
	SobolSampler sampler(&rnd, NULL, NULL, 0.f, &sharedData);
	
	// Request the samples
	const u_int sampleBootSize = 5;
	const u_int sampleStepSize = 3;
	const u_int sampleSize = 
		sampleBootSize + // To generate eye ray
		maxDepth * sampleStepSize; // For each path vertex
	sampler.RequestSamples(sampleSize);
	
	// Initialize SampleResult 
	vector<SampleResult> sampleResults(1);
	SampleResult &sampleResult = sampleResults[0];
	sampleResult.Init(Film::RADIANCE_PER_PIXEL_NORMALIZED, 1);

	// Initialize the max. path depth
	PathDepthInfo maxPathDepth;
	maxPathDepth.depth = maxDepth;
	maxPathDepth.diffuseDepth = maxDepth;
	maxPathDepth.glossyDepth = maxDepth;
	maxPathDepth.specularDepth = maxDepth;

	double lastPrintTime = WallClockTime();
	u_int cacheLookUp = 0;
	u_int cacheHits = 0;
	for (u_int i = 0; i < sampleCount; ++i) {
		const double now = WallClockTime();
		if (now - lastPrintTime > 2.0) {
			SLG_LOG("Direct light sampling cache samples: " << i << "/" << sampleCount <<" (" << (u_int)((100.0 * i) / sampleCount) << "%)");
			lastPrintTime = now;
		}
		
		sampleResult.radiance[0] = Spectrum();
		
		Ray eyeRay;
		PathVolumeInfo volInfo;
		GenerateEyeRay(scene->camera, eyeRay, volInfo, &sampler, sampleResult);
		
		BSDFEvent lastBSDFEvent = SPECULAR;
		Spectrum pathThroughput(1.f);
		PathDepthInfo depthInfo;
		BSDF bsdf;
		for (;;) {
			sampleResult.firstPathVertex = (depthInfo.depth == 0);
			const u_int sampleOffset = sampleBootSize + depthInfo.depth * sampleStepSize;

			RayHit eyeRayHit;
			Spectrum connectionThroughput;
			const bool hit = scene->Intersect(NULL, false, sampleResult.firstPathVertex,
					&volInfo, sampler.GetSample(sampleOffset),
					&eyeRay, &eyeRayHit, &bsdf, &connectionThroughput,
					&pathThroughput, &sampleResult);
			pathThroughput *= connectionThroughput;
			// Note: pass-through check is done inside Scene::Intersect()

			if (!hit) {
				// Nothing was hit, time to stop
				break;
			}

			//------------------------------------------------------------------
			// Something was hit
			//------------------------------------------------------------------

			// Check if a cache entry is available for this point
			if (octree->GetEntry(bsdf.hitPoint.p, bsdf.hitPoint.geometryN))
				++cacheHits;
			else {
				// TODO: add support for volumes
				DLSCacheEntry *entry = new DLSCacheEntry(bsdf.hitPoint.p, bsdf.hitPoint.geometryN);
				octree->Add(entry);
			}
			++cacheLookUp;
			
			//------------------------------------------------------------------
			// Build the next vertex path ray
			//------------------------------------------------------------------

			// Check if I reached the max. depth
			sampleResult.lastPathVertex = depthInfo.IsLastPathVertex(maxPathDepth, bsdf.GetEventTypes());
			if (sampleResult.lastPathVertex && !sampleResult.firstPathVertex)
				break;

			Vector sampledDir;
			float cosSampledDir, lastPdfW;
			const Spectrum bsdfSample = bsdf.Sample(&sampledDir,
						sampler.GetSample(sampleOffset + 1),
						sampler.GetSample(sampleOffset + 2),
						&lastPdfW, &cosSampledDir, &lastBSDFEvent);
			sampleResult.passThroughPath = false;

			assert (!bsdfSample.IsNaN() && !bsdfSample.IsInf());
			if (bsdfSample.Black())
				break;
			assert (!isnan(lastPdfW) && !isinf(lastPdfW));

			if (sampleResult.firstPathVertex)
				sampleResult.firstPathVertexEvent = lastBSDFEvent;

			// Increment path depth informations
			depthInfo.IncDepths(lastBSDFEvent);

			pathThroughput *= bsdfSample;
			assert (!pathThroughput.IsNaN() && !pathThroughput.IsInf());

			// Update volume information
			volInfo.Update(lastBSDFEvent, bsdf);

			eyeRay.Update(bsdf.hitPoint.p, sampledDir);
		}
		
		sampler.NextSample(sampleResults);
	}

	SLG_LOG("Direct light sampling cache hits: " << cacheHits << "/" << cacheLookUp <<" (" << (u_int)((100.0 * cacheHits) / cacheLookUp) << "%)");
	
	// Export the otcre for debugging
	octree->DebugExport("octree-point.scn", .025f);
}
