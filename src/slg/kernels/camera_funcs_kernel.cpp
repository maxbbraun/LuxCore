#include <string>
namespace slg { namespace ocl {
std::string KernelSource_camera_funcs = 
"#line 2 \"camera_funcs.cl\"\n"
"\n"
"/***************************************************************************\n"
" * Copyright 1998-2015 by authors (see AUTHORS.txt)                        *\n"
" *                                                                         *\n"
" *   This file is part of LuxRender.                                       *\n"
" *                                                                         *\n"
" * Licensed under the Apache License, Version 2.0 (the \"License\");         *\n"
" * you may not use this file except in compliance with the License.        *\n"
" * You may obtain a copy of the License at                                 *\n"
" *                                                                         *\n"
" *     http://www.apache.org/licenses/LICENSE-2.0                          *\n"
" *                                                                         *\n"
" * Unless required by applicable law or agreed to in writing, software     *\n"
" * distributed under the License is distributed on an \"AS IS\" BASIS,       *\n"
" * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.*\n"
" * See the License for the specific language governing permissions and     *\n"
" * limitations under the License.                                          *\n"
" ***************************************************************************/\n"
"\n"
"#if defined(PARAM_CAMERA_ENABLE_OCULUSRIFT_BARREL)\n"
"void Camera_OculusRiftBarrelPostprocess(const float x, const float y, float *barrelX, float *barrelY) {\n"
"	// Express the sample in coordinates relative to the eye center\n"
"	float ex = x * 2.f - 1.f;\n"
"	float ey = y * 2.f - 1.f;\n"
"\n"
"	if ((ex == 0.f) && (ey == 0.f)) {\n"
"		*barrelX = 0.f;\n"
"		*barrelY = 0.f;\n"
"		return;\n"
"	}\n"
"\n"
"	// Distance from the eye center\n"
"	const float distance = sqrt(ex * ex + ey * ey);\n"
"\n"
"	// \"Push\" the sample away base on the distance from the center\n"
"	const float scale = 1.f / 1.4f;\n"
"	const float k0 = 1.f;\n"
"	const float k1 = .22f;\n"
"	const float k2 = .23f;\n"
"	const float k3 = 0.f;\n"
"	const float distance2 = distance * distance;\n"
"	const float distance4 = distance2 * distance2;\n"
"	const float distance6 = distance2 * distance4;\n"
"	const float fr = scale * (k0 + k1 * distance2 + k2 * distance4 + k3 * distance6);\n"
"\n"
"	ex *= fr;\n"
"	ey *= fr;\n"
"\n"
"	// Clamp the coordinates\n"
"	ex = clamp(ex, -1.f, 1.f);\n"
"	ey = clamp(ey, -1.f, 1.f);\n"
"\n"
"	*barrelX = (ex + 1.f) * .5f;\n"
"	*barrelY = (ey + 1.f) * .5f;\n"
"}\n"
"#endif\n"
"\n"
"#if defined(PARAM_CAMERA_ENABLE_CLIPPING_PLANE)\n"
"void Camera_ApplyArbitraryClippingPlane(\n"
"		__global const Camera* restrict camera,\n"
"#if !defined(CAMERA_GENERATERAY_PARAM_MEM_SPACE_PRIVATE)\n"
"		__global\n"
"#endif\n"
"		Ray *ray) {\n"
"	const float3 rayOrig = (float3)(ray->o.x, ray->o.y, ray->o.z);\n"
"	const float3 rayDir = (float3)(ray->d.x, ray->d.y, ray->d.z);\n"
"\n"
"	const float3 clippingPlaneCenter = (float3)(camera->clippingPlaneCenter.x, camera->clippingPlaneCenter.y, camera->clippingPlaneCenter.z);\n"
"	const float3 clippingPlaneNormal = (float3)(camera->clippingPlaneNormal.x, camera->clippingPlaneNormal.y, camera->clippingPlaneNormal.z);\n"
"\n"
"	// Intersect the ray with clipping plane\n"
"	const float denom = dot(clippingPlaneNormal, rayDir);\n"
"	const float3 pr = clippingPlaneCenter - rayOrig;\n"
"	float d = dot(pr, clippingPlaneNormal);\n"
"\n"
"	if (fabs(denom) > DEFAULT_COS_EPSILON_STATIC) {\n"
"		// There is a valid intersection\n"
"		d /= denom; \n"
"\n"
"		if (d > 0.f) {\n"
"			// The plane is in front of the camera\n"
"			if (denom < 0.f) {\n"
"				// The plane points toward the camera\n"
"				ray->maxt = clamp(d, ray->mint, ray->maxt);\n"
"			} else {\n"
"				// The plane points away from the camera\n"
"				ray->mint = clamp(d, ray->mint, ray->maxt);\n"
"			}\n"
"		} else {\n"
"			if ((denom < 0.f) && (d < 0.f)) {\n"
"				// No intersection possible, I use a trick here to avoid any\n"
"				// intersection by setting mint=maxt\n"
"				ray->mint = ray->maxt;\n"
"			} else {\n"
"				// Nothing to do\n"
"			}\n"
"		}\n"
"	} else {\n"
"		// The plane is parallel to the view directions. Check if I'm on the\n"
"		// visible side of the plane or not\n"
"		if (d >= 0.f) {\n"
"			// No intersection possible, I use a trick here to avoid any\n"
"			// intersection by setting mint=maxt\n"
"			ray->mint = ray->maxt;\n"
"		} else {\n"
"			// Nothing to do\n"
"		}\n"
"	}\n"
"}\n"
"#endif\n"
"\n"
"//------------------------------------------------------------------------------\n"
"// Perspective camera\n"
"//------------------------------------------------------------------------------\n"
"\n"
"#if (PARAM_CAMERA_TYPE == 0)\n"
"\n"
"void Camera_GenerateRay(\n"
"		__global const Camera* restrict camera,\n"
"		const uint filmWidth, const uint filmHeight,\n"
"#if !defined(CAMERA_GENERATERAY_PARAM_MEM_SPACE_PRIVATE)\n"
"		__global\n"
"#endif\n"
"		Ray *ray,\n"
"		const float filmX, const float filmY, const float timeSample\n"
"#if defined(PARAM_CAMERA_HAS_DOF)\n"
"		, const float dofSampleX, const float dofSampleY\n"
"#endif\n"
"		) {\n"
"#if defined(PARAM_CAMERA_ENABLE_OCULUSRIFT_BARREL)\n"
"	float ssx, ssy;\n"
"	Camera_OculusRiftBarrelPostprocess(filmX / filmWidth, (filmHeight - filmY - 1.f) / filmHeight, &ssx, &ssy);\n"
"	const float3 Pras = (float3) (min(ssx * filmWidth, (float) (filmWidth - 1)), min(ssy * filmHeight, (float) (filmHeight - 1)), 0.f);\n"
"#else\n"
"	const float3 Pras = (float3) (filmX, filmHeight - filmY - 1.f, 0.f);\n"
"#endif\n"
"\n"
"	float3 rayOrig = Transform_ApplyPoint(&camera->base.rasterToCamera, Pras);\n"
"	float3 rayDir = rayOrig;\n"
"\n"
"	const float hither = camera->base.hither;\n"
"\n"
"#if defined(PARAM_CAMERA_HAS_DOF)\n"
"	// Sample point on lens\n"
"	float lensU, lensV;\n"
"	ConcentricSampleDisk(dofSampleX, dofSampleY, &lensU, &lensV);\n"
"	const float lensRadius = camera->persp.projCamera.lensRadius;\n"
"	lensU *= lensRadius;\n"
"	lensV *= lensRadius;\n"
"\n"
"	// Compute point on plane of focus\n"
"	const float focalDistance = camera->persp.projCamera.focalDistance;\n"
"	const float dist = focalDistance - hither;\n"
"\n"
"	const float ft = dist / rayDir.z;\n"
"	const float3 Pfocus = rayOrig + rayDir * ft;\n"
"\n"
"	// Update ray for effect of lens\n"
"	const float k = dist / focalDistance;\n"
"	rayOrig.x += lensU * k;\n"
"	rayOrig.y += lensV * k;\n"
"\n"
"	rayDir = Pfocus - rayOrig;\n"
"#endif\n"
"\n"
"	rayDir = normalize(rayDir);\n"
"\n"
"	const float maxt = (camera->base.yon - hither) / rayDir.z;\n"
"	const float time = mix(camera->base.shutterOpen, camera->base.shutterClose, timeSample);\n"
"\n"
"	// Transform ray in world coordinates\n"
"	rayOrig = Transform_ApplyPoint(&camera->base.cameraToWorld, rayOrig);\n"
"	rayDir = Transform_ApplyVector(&camera->base.cameraToWorld, rayDir);\n"
"\n"
"	const uint interpolatedTransformFirstIndex = camera->base.motionSystem.interpolatedTransformFirstIndex;\n"
"	if (interpolatedTransformFirstIndex != NULL_INDEX) {\n"
"		Matrix4x4 m;\n"
"		MotionSystem_Sample(&camera->base.motionSystem, time, camera->base.interpolatedTransforms, &m);\n"
"\n"
"		rayOrig = Matrix4x4_ApplyPoint_Private(&m, rayOrig);\n"
"		rayDir = Matrix4x4_ApplyVector_Private(&m, rayDir);\n"
"	}\n"
"\n"
"#if defined(CAMERA_GENERATERAY_PARAM_MEM_SPACE_PRIVATE)\n"
"	Ray_Init3_Private(ray, rayOrig, rayDir, maxt, time);\n"
"#else\n"
"	Ray_Init3(ray, rayOrig, rayDir, maxt, time);\n"
"#endif\n"
"\n"
"#if defined(PARAM_CAMERA_ENABLE_CLIPPING_PLANE)\n"
"	Camera_ApplyArbitraryClippingPlane(camera, ray);\n"
"#endif\n"
"\n"
"	/*printf(\"(%f, %f, %f) (%f, %f, %f) [%f, %f]\\n\",\n"
"		ray->o.x, ray->o.y, ray->o.z, ray->d.x, ray->d.y, ray->d.z,\n"
"		ray->mint, ray->maxt);*/\n"
"}\n"
"\n"
"#endif\n"
"\n"
"//------------------------------------------------------------------------------\n"
"// Orthographic camera\n"
"//------------------------------------------------------------------------------\n"
"\n"
"#if (PARAM_CAMERA_TYPE == 1)\n"
"\n"
"void Camera_GenerateRay(\n"
"		__global const Camera* restrict camera,\n"
"		const uint filmWidth, const uint filmHeight,\n"
"#if !defined(CAMERA_GENERATERAY_PARAM_MEM_SPACE_PRIVATE)\n"
"		__global\n"
"#endif\n"
"		Ray *ray,\n"
"		const float filmX, const float filmY, const float timeSample\n"
"#if defined(PARAM_CAMERA_HAS_DOF)\n"
"		, const float dofSampleX, const float dofSampleY\n"
"#endif\n"
"		) {\n"
"	const float3 Pras = (float3) (filmX, filmHeight - filmY - 1.f, 0.f);\n"
"	float3 rayOrig = Transform_ApplyPoint(&camera->base.rasterToCamera, Pras);\n"
"	float3 rayDir = (float3)(0.f, 0.f, 1.f);\n"
"\n"
"	const float hither = camera->base.hither;\n"
"\n"
"#if defined(PARAM_CAMERA_HAS_DOF)\n"
"	// Sample point on lens\n"
"	float lensU, lensV;\n"
"	ConcentricSampleDisk(dofSampleX, dofSampleY, &lensU, &lensV);\n"
"	const float lensRadius = camera->ortho.projCamera.lensRadius;\n"
"	lensU *= lensRadius;\n"
"	lensV *= lensRadius;\n"
"\n"
"	// Compute point on plane of focus\n"
"	const float focalDistance = camera->ortho.projCamera.focalDistance;\n"
"	const float dist = focalDistance - hither;\n"
"\n"
"	const float ft = dist;\n"
"	const float3 Pfocus = rayOrig + rayDir * ft;\n"
"\n"
"	// Update ray for effect of lens\n"
"	const float k = dist / focalDistance;\n"
"	rayOrig.x += lensU * k;\n"
"	rayOrig.y += lensV * k;\n"
"\n"
"	rayDir = Pfocus - rayOrig;\n"
"#endif\n"
"\n"
"	rayDir = normalize(rayDir);\n"
"\n"
"	const float maxt = (camera->base.yon - hither);\n"
"	const float time = mix(camera->base.shutterOpen, camera->base.shutterClose, timeSample);\n"
"\n"
"	// Transform ray in world coordinates\n"
"	rayOrig = Transform_ApplyPoint(&camera->base.cameraToWorld, rayOrig);\n"
"	rayDir = Transform_ApplyVector(&camera->base.cameraToWorld, rayDir);\n"
"\n"
"	const uint interpolatedTransformFirstIndex = camera->base.motionSystem.interpolatedTransformFirstIndex;\n"
"	if (interpolatedTransformFirstIndex != NULL_INDEX) {\n"
"		Matrix4x4 m;\n"
"		MotionSystem_Sample(&camera->base.motionSystem, time, camera->base.interpolatedTransforms, &m);\n"
"\n"
"		rayOrig = Matrix4x4_ApplyPoint_Private(&m, rayOrig);\n"
"		rayDir = Matrix4x4_ApplyVector_Private(&m, rayDir);\n"
"	}\n"
"\n"
"#if defined(CAMERA_GENERATERAY_PARAM_MEM_SPACE_PRIVATE)\n"
"	Ray_Init3_Private(ray, rayOrig, rayDir, maxt, time);\n"
"#else\n"
"	Ray_Init3(ray, rayOrig, rayDir, maxt, time);\n"
"#endif\n"
"\n"
"#if defined(PARAM_CAMERA_ENABLE_CLIPPING_PLANE)\n"
"	Camera_ApplyArbitraryClippingPlane(camera, ray);\n"
"#endif\n"
"\n"
"	/*printf(\"(%f, %f, %f) (%f, %f, %f) [%f, %f]\\n\",\n"
"		ray->o.x, ray->o.y, ray->o.z, ray->d.x, ray->d.y, ray->d.z,\n"
"		ray->mint, ray->maxt);*/\n"
"}\n"
"\n"
"#endif\n"
"\n"
"//------------------------------------------------------------------------------\n"
"// Stereo camera\n"
"//------------------------------------------------------------------------------\n"
"\n"
"#if (PARAM_CAMERA_TYPE == 2)\n"
"\n"
"void Camera_GenerateRay(\n"
"		__global const Camera* restrict camera,\n"
"		const uint origFilmWidth, const uint filmHeight,\n"
"#if !defined(CAMERA_GENERATERAY_PARAM_MEM_SPACE_PRIVATE)\n"
"		__global\n"
"#endif\n"
"		Ray *ray,\n"
"		const float origFilmX, const float filmY, const float timeSample\n"
"#if defined(PARAM_CAMERA_HAS_DOF)\n"
"		, const float dofSampleX, const float dofSampleY\n"
"#endif\n"
"		) {\n"
"	__global const Transform* restrict rasterToCamera;\n"
"	__global const Transform* restrict cameraToWorld;\n"
"	float filmX;\n"
"	const float filmWidth = origFilmWidth /2 ;\n"
"	if (origFilmX < filmWidth) {\n"
"		rasterToCamera = &camera->stereo.leftEyeRasterToCamera;\n"
"		cameraToWorld = &camera->stereo.leftEyeCameraToWorld;\n"
"		filmX = origFilmX;\n"
"	} else {\n"
"		rasterToCamera = &camera->stereo.rightEyeRasterToCamera;\n"
"		cameraToWorld = &camera->stereo.rightEyeCameraToWorld;\n"
"		filmX = origFilmX - filmWidth;\n"
"	}\n"
"\n"
"#if defined(PARAM_CAMERA_ENABLE_OCULUSRIFT_BARREL)\n"
"	float ssx, ssy;\n"
"	Camera_OculusRiftBarrelPostprocess(filmX / filmWidth, (filmHeight - filmY - 1.f) / filmHeight, &ssx, &ssy);\n"
"	const float3 Pras = (float3) (min(ssx * filmWidth, (float) (filmWidth - 1)), min(ssy * filmHeight, (float) (filmHeight - 1)), 0.f);\n"
"#else\n"
"	const float3 Pras = (float3) (filmX, filmHeight - filmY - 1.f, 0.f);\n"
"#endif\n"
"\n"
"	float3 rayOrig = Transform_ApplyPoint(rasterToCamera, Pras);\n"
"	float3 rayDir = rayOrig;\n"
"\n"
"	const float hither = camera->base.hither;\n"
"\n"
"#if defined(PARAM_CAMERA_HAS_DOF)\n"
"	// Sample point on lens\n"
"	float lensU, lensV;\n"
"	ConcentricSampleDisk(dofSampleX, dofSampleY, &lensU, &lensV);\n"
"	const float lensRadius = camera->persp.projCamera.lensRadius;\n"
"	lensU *= lensRadius;\n"
"	lensV *= lensRadius;\n"
"\n"
"	// Compute point on plane of focus\n"
"	const float focalDistance = camera->persp.projCamera.focalDistance;\n"
"	const float dist = focalDistance - hither;\n"
"\n"
"	const float ft = dist / rayDir.z;\n"
"	const float3 Pfocus = rayOrig + rayDir * ft;\n"
"\n"
"	// Update ray for effect of lens\n"
"	const float k = dist / focalDistance;\n"
"	rayOrig.x += lensU * k;\n"
"	rayOrig.y += lensV * k;\n"
"\n"
"	rayDir = Pfocus - rayOrig;\n"
"#endif\n"
"\n"
"	rayDir = normalize(rayDir);\n"
"\n"
"	const float maxt = (camera->base.yon - hither) / rayDir.z;\n"
"	const float time = mix(camera->base.shutterOpen, camera->base.shutterClose, timeSample);\n"
"\n"
"	// Transform ray in world coordinates\n"
"	rayOrig = Transform_ApplyPoint(cameraToWorld, rayOrig);\n"
"	rayDir = Transform_ApplyVector(cameraToWorld, rayDir);\n"
"\n"
"	const uint interpolatedTransformFirstIndex = camera->base.motionSystem.interpolatedTransformFirstIndex;\n"
"	if (interpolatedTransformFirstIndex != NULL_INDEX) {\n"
"		Matrix4x4 m;\n"
"		MotionSystem_Sample(&camera->base.motionSystem, time, camera->base.interpolatedTransforms, &m);\n"
"\n"
"		rayOrig = Matrix4x4_ApplyPoint_Private(&m, rayOrig);\n"
"		rayDir = Matrix4x4_ApplyVector_Private(&m, rayDir);\n"
"	}\n"
"\n"
"#if defined(CAMERA_GENERATERAY_PARAM_MEM_SPACE_PRIVATE)\n"
"	Ray_Init3_Private(ray, rayOrig, rayDir, maxt, time);\n"
"#else\n"
"	Ray_Init3(ray, rayOrig, rayDir, maxt, time);\n"
"#endif\n"
"\n"
"#if defined(PARAM_CAMERA_ENABLE_CLIPPING_PLANE)\n"
"	Camera_ApplyArbitraryClippingPlane(camera, ray);\n"
"#endif\n"
"\n"
"	/*printf(\"(%f, %f, %f) (%f, %f, %f) [%f, %f]\\n\",\n"
"		ray->o.x, ray->o.y, ray->o.z, ray->d.x, ray->d.y, ray->d.z,\n"
"		ray->mint, ray->maxt);*/\n"
"}\n"
"\n"
"#endif\n"
; } }
