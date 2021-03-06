// Copyright (c) 2017, Danilo Peixoto. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice, this
//   list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
//
// * Neither the name of the copyright holder nor the names of its
//   contributors may be used to endorse or promote products derived from
//   this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "femMesh.h"

#include <maya/MFnNumericAttribute.h>
#include <maya/MFnTypedAttribute.h>
#include <maya/MFnNumericData.h>
#include <maya/MFnMeshData.h>
#include <maya/MFnIntArrayData.h>
#include <maya/MDataHandle.h>
#include <maya/MFnMesh.h>
#include <maya/MPointArray.h>

#include <nglib.h>

MObject FEMMesh::maximumElementSizeObject;
MObject FEMMesh::gradingObject;
MObject FEMMesh::inputMeshObject;
MObject FEMMesh::outputMeshObject;
MObject FEMMesh::surfaceNodesObject;
MObject FEMMesh::volumeNodesObject;

const MTypeId FEMMesh::id(0x00128581);
const MString FEMMesh::typeName("femMesh");

FEMMesh::FEMMesh() {}
FEMMesh::~FEMMesh() {}

void * FEMMesh::creator() {
    return new FEMMesh;
}
MStatus FEMMesh::initialize() {
    MFnNumericAttribute numericAttribute;
    MFnTypedAttribute typedAttribute;

    maximumElementSizeObject = numericAttribute.create("maximumElementSize", "mel",
        MFnNumericData::kDouble, 1.0);
    numericAttribute.setMin(1.0e-3);
    numericAttribute.setSoftMax(1.0);
    numericAttribute.setStorable(true);
    numericAttribute.setKeyable(true);

    gradingObject = numericAttribute.create("grading", "g", MFnNumericData::kDouble, 0.25);
    numericAttribute.setMin(0);
    numericAttribute.setMax(1.0);
    numericAttribute.setStorable(true);
    numericAttribute.setKeyable(true);

    inputMeshObject = typedAttribute.create("inputMesh", "in", MFnMeshData::kMesh);
    typedAttribute.setStorable(true);

    outputMeshObject = typedAttribute.create("outputMesh", "out", MFnMeshData::kMesh);
    typedAttribute.setWritable(false);
    typedAttribute.setStorable(false);

    surfaceNodesObject = typedAttribute.create("surfaceNodes", "sn", MFnIntArrayData::kIntArray);
    typedAttribute.setWritable(false);
    typedAttribute.setStorable(false);

    volumeNodesObject = typedAttribute.create("volumeNodes", "vn", MFnIntArrayData::kIntArray);
    typedAttribute.setWritable(false);
    typedAttribute.setStorable(false);

    addAttribute(maximumElementSizeObject);
    addAttribute(gradingObject);
    addAttribute(inputMeshObject);
    addAttribute(outputMeshObject);
    addAttribute(surfaceNodesObject);
    addAttribute(volumeNodesObject);

    attributeAffects(maximumElementSizeObject, outputMeshObject);
    attributeAffects(maximumElementSizeObject, surfaceNodesObject);
    attributeAffects(maximumElementSizeObject, volumeNodesObject);
    attributeAffects(gradingObject, outputMeshObject);
    attributeAffects(gradingObject, surfaceNodesObject);
    attributeAffects(gradingObject, volumeNodesObject);
    attributeAffects(inputMeshObject, outputMeshObject);
    attributeAffects(inputMeshObject, surfaceNodesObject);
    attributeAffects(inputMeshObject, volumeNodesObject);

    return MS::kSuccess;
}
MStatus FEMMesh::compute(const MPlug & plug, MDataBlock & data) {
    MStatus status;

    if (plug != outputMeshObject && plug != surfaceNodesObject
        && plug != volumeNodesObject)
        return MS::kUnknownParameter;

    MDataHandle maximumElementSizeHandle = data.inputValue(maximumElementSizeObject, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    MDataHandle gradingHandle = data.inputValue(gradingObject, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    MDataHandle inputMeshHandle = data.inputValue(inputMeshObject, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    MDataHandle outputMeshHandle = data.outputValue(outputMeshObject);
    MDataHandle surfaceNodesHandle = data.outputValue(surfaceNodesObject);
    MDataHandle volumeNodesHandle = data.outputValue(volumeNodesObject);

    double maximumElementSize = maximumElementSizeHandle.asDouble();
    double grading = gradingHandle.asDouble();

    outputMeshHandle.set(inputMeshHandle.asMesh());
    MObject meshObject = outputMeshHandle.asMesh();

    if (meshObject.isNull())
        return MS::kFailure;

    MIntArray surfaceNodes, volumeNodes;

    status = tetrahedralize(meshObject, surfaceNodes, volumeNodes,
        maximumElementSize, grading);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    MFnIntArrayData intArrayData;

    surfaceNodesHandle.set(intArrayData.create(surfaceNodes));
    volumeNodesHandle.set(intArrayData.create(volumeNodes));

    data.setClean(plug);

    return MS::kSuccess;
}

MStatus FEMMesh::tetrahedralize(MObject & meshObject, MIntArray & surfaceNodes,
    MIntArray & volumeNodes, double maximumElementSize, double grading) const {
    MStatus status;

    MFnMesh mesh(meshObject);

    MPointArray vertexArray;
    mesh.getPoints(vertexArray);

    MIntArray triangleCounts, triangleVertices;
    mesh.getTriangles(triangleCounts, triangleVertices);

    nglib::Initialize();

    nglib::STLGeometry * surfaceMesh = nglib::STLNewGeometry();
    nglib::Mesh * volumeMesh = nglib::NewMesh();

    nglib::MeshingParameters parameters;
    parameters.max_element_size = maximumElementSize;
    parameters.grading = grading;
    parameters.second_order = false;

    int triangleCount = triangleVertices.length() / 3;

    for (int i = 0; i < triangleCount; i++) {
        double points[12];

        for (int j = 0; j < 3; j++) {
            double * point = &points[j * 4];

            vertexArray[triangleVertices[i * 3 + j]].get(point);
        }

        nglib::STLAddTriangle(surfaceMesh, &points[0], &points[4], &points[8]);
    }

    vertexArray.setLength(0);
    triangleVertices.setLength(0);

    nglib::Status result = nglib::STLInitializeGeometry(surfaceMesh,
        volumeMesh, &parameters);

    if (result != nglib::Status::Success)
        return MS::kFailure;

    result = nglib::STLGenerateSurfaceMesh(surfaceMesh, volumeMesh, &parameters);

    if (result != nglib::Status::Success)
        return MS::kFailure;

    result = nglib::GenerateVolumeMesh(volumeMesh, &parameters);

    if (result != nglib::Status::Success)
        return MS::kFailure;

    int pointCount = nglib::GetPointCount(volumeMesh);
    int surfaceCount = nglib::GetSurfaceCount(volumeMesh);
    int volumeCount = nglib::GetVolumeCount(volumeMesh);

    triangleCount = volumeCount * 4;

    vertexArray.setLength(pointCount);
    surfaceNodes.setLength(surfaceCount * 3);
    volumeNodes.setLength(triangleCount);

    triangleVertices.setLength(triangleCount * 3);

    for (int i = 0; i < pointCount; i++) {
        double point[3];

        nglib::GetPoint(volumeMesh, i, point);

        vertexArray.set(i, point[0], point[1], point[2]);
    }

    for (int i = 0; i < surfaceCount; i++) {
        int triangle[3];

        nglib::GetSurfaceElement(volumeMesh, i, triangle);

        for (int j = 0; j < 3; j++)
            surfaceNodes[i * 3 + j] = triangle[j];
    }

    for (int i = 0; i < volumeCount; i++) {
        int tetrahedron[4];

        nglib::GetVolumeElement(volumeMesh, i, tetrahedron);

        volumeNodes[i * 4] = tetrahedron[0];
        volumeNodes[i * 4 + 1] = tetrahedron[1];
        volumeNodes[i * 4 + 2] = tetrahedron[3];
        volumeNodes[i * 4 + 3] = tetrahedron[2];

        int s = i * 12;

        triangleVertices[s] = tetrahedron[0];
        triangleVertices[s + 1] = tetrahedron[1];
        triangleVertices[s + 2] = tetrahedron[2];

        triangleVertices[s + 3] = tetrahedron[1];
        triangleVertices[s + 4] = tetrahedron[3];
        triangleVertices[s + 5] = tetrahedron[2];

        triangleVertices[s + 6] = tetrahedron[0];
        triangleVertices[s + 7] = tetrahedron[2];
        triangleVertices[s + 8] = tetrahedron[3];

        triangleVertices[s + 9] = tetrahedron[0];
        triangleVertices[s + 10] = tetrahedron[3];
        triangleVertices[s + 11] = tetrahedron[1];
    }

    nglib::DeleteMesh(volumeMesh);
    nglib::STLDeleteGeometry(surfaceMesh);

    for (int i = 0; i < triangleCount; i++) {
        const int * ti = &triangleVertices[i * 3];

        for (int j = i + 1; j < triangleCount; j++) {
            const int * tj = &triangleVertices[j * 3];

            if (compareTriangle(ti, tj)) {
                triangleVertices.remove(j * 3 + 2);
                triangleVertices.remove(j * 3 + 1);
                triangleVertices.remove(j * 3);

                triangleCount--;

                break;
            }
        }
    }

    triangleVertices.setLength(triangleCount * 3);

    mesh.create(pointCount, triangleCount, vertexArray,
        MIntArray(triangleCount, 3), triangleVertices, meshObject);

    for (int i = 0; i < mesh.numEdges(); i++)
        mesh.setEdgeSmoothing(i, false);

    mesh.cleanupEdgeSmoothing();

    return MS::kSuccess;
}
bool FEMMesh::compareTriangle(const int * lhs, const int * rhs) const {
    bool t0 = lhs[0] == rhs[0] || lhs[0] == rhs[1] || lhs[0] == rhs[2];
    bool t1 = lhs[1] == rhs[0] || lhs[1] == rhs[1] || lhs[1] == rhs[2];
    bool t2 = lhs[2] == rhs[0] || lhs[2] == rhs[1] || lhs[2] == rhs[2];

    return t0 && t1 && t2;
}