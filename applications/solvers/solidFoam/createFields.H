Info << "Creating mesh parameters ..." << endl;

// Operations class
operations op(mesh);

// Point mesh
pointMesh pMesh(mesh);

// Material face area
const surfaceScalarField& magSf = mesh.magSf();

// Material face area normal vector
const surfaceVectorField& Sf = mesh.Sf();

// Minimum edge length
const dimensionedScalar& h = op.minimumEdgeLength();

// Boundary patches
const polyBoundaryMesh& bm = mesh.boundaryMesh();
const label& symmetricPatchID = bm.findPatchID("symmetric");
const label& symmetricXpatchID = bm.findPatchID("symmetricX");
const label& symmetricYpatchID = bm.findPatchID("symmetricY");
const label& symmetricZpatchID = bm.findPatchID("symmetricZ");


Info << "Creating mesh coordinate fields ..." << endl;

// Material cell center coordinates
const volVectorField& C = mesh.C();

// Spatial cell center coordinates
volVectorField x
(
    IOobject("x", mesh),
    C
);

// Spatial nodal coordinates
pointVectorField xN
(
    IOobject("xN", mesh),
    pMesh,
    dimensionedVector("xN", dimensionSet(0,1,0,0,0,0,0), vector::zero)
);
xN.primitiveFieldRef() = mesh.points();

// Material nodal coordinates
pointVectorField XN = xN;

// Spatial face center coordinates
surfaceVectorField xF = mesh.Cf();


Info << "Creating mesh normal fields ..." << endl;

 // Material normals
 const surfaceVectorField N = Sf/mesh.magSf();

 // Spatial normals
 surfaceVectorField n
 (
    IOobject("n", mesh),
    N
 );

// Normal projection tensor
surfaceTensorField nCn
(
    IOobject("nCn", mesh),
    n*n
);

// Tangential projection tensor
surfaceTensorField iMnCn
(
    IOobject("iMnCn", mesh),
    tensor::I - nCn
);


Info << "Creating linear momentum fields ..." << endl;

// Cell linear momentum
volVectorField lm
(
    IOobject
    (
        "lm",
        runTime.timeName(),
        mesh,
        IOobject::READ_IF_PRESENT,
        IOobject::AUTO_WRITE
    ),
    mesh,
    dimensionedVector("lm", dimensionSet(1,-2,-1,0,0,0,0), vector::zero)
);

// Nodal linear momentum
pointVectorField lmN
(
    IOobject
    (
        "lmN",
        runTime.timeName(),
        mesh,
        IOobject::MUST_READ,
        IOobject::AUTO_WRITE
    ),
    pMesh
);


Info << "Creating strain measure fields ..." << endl;

// Deformation gradient tensor
volTensorField F
(
    IOobject("F", mesh),
    mesh,
    Foam::tensor::I
);

// Cofactor of deformation
volTensorField H
(
    IOobject("H", mesh),
    det(F)*op.invT(F)
);

// Jacobian of deformation
volScalarField J
(
    IOobject("J", mesh),
    det(F)
);


Info << "Creating constitutive model ..." << endl;

// Solid model class
solidModel model(F, mechanicalProperties);
model.correct();

// Density
const dimensionedScalar& rho = model.density();

// Pressure
volScalarField p = model.pressure();

// First Piola Kirchhoff stress tensor
volTensorField P = model.piola();
volVectorField Px = op.decomposeTensorX(P);
volVectorField Py = op.decomposeTensorY(P);
volVectorField Pz = op.decomposeTensorZ(P);


Info << "Creating fields for wave speeds ..." << endl;

// Continuum mechanics class
mechanics mech(mesh, mechanicalProperties);

// Longitudinal wave speed
volScalarField Up
(
    IOobject("Up", mesh),
    mesh,
    model.Up()/beta
);

// Shear wave speed
volScalarField Us
(
    IOobject ("Us", mesh),
    mesh,
    model.Us()*beta
);

// Stretch
volScalarField stretch = mech.stretch();

// Wave speed for time increment
volScalarField Up_time = model.Up()/mech.stretch();


Info << "Creating fields for gradient ..." << endl;

// Gradient class
gradientSchemes grad(mesh);

// Gradient of cell linear momentum
volTensorField lmGrad = grad.gradient(lm);

// Gradient of first Piola Kirchhoff stress tensor
volTensorField PxGrad = grad.gradient(Px);
volTensorField PyGrad = grad.gradient(Py);
volTensorField PzGrad = grad.gradient(Pz);


Info << "Creating fields for reconstruction ..." << endl;

// Reconstruction of linear momentum
surfaceVectorField lm_M
(
    IOobject
    (
        "lm_M",
        runTime.timeName(),
        mesh,
        IOobject::NO_READ,
        IOobject::AUTO_WRITE
    ),
    mesh,
    dimensionedVector("lm_M", lm.dimensions(), vector::zero)
);
surfaceVectorField lm_P = lm_M;
grad.reconstruct(lm, lmGrad, lm_M, lm_P);

// Reconstruction of PK1 stresses
surfaceTensorField P_M
(
    IOobject("P_M", mesh),
    mesh,
    dimensionedTensor("P_M", P.dimensions(), tensor::zero)
);
surfaceTensorField P_P = P_M;
grad.reconstruct(P, PxGrad, PyGrad, PzGrad, P_M, P_P);

// Reconstruction of traction
surfaceVectorField t_M
(
    IOobject
    (
        "t_M",
        runTime.timeName(),
        mesh,
        IOobject::NO_READ,
        IOobject::AUTO_WRITE
    ),
    P_M & N
);
surfaceVectorField t_P = P_P & N;


Info << "Creating fields for riemann solver ..." << endl;

// Contact linear momentum
surfaceVectorField lmC =
(
    0.5*(lm_M + lm_P) + (0.5*((nCn/fvc::interpolate(Up))
  + (iMnCn/fvc::interpolate(Us))) & (t_P - t_M))
);

volVectorField lm_b
(
    IOobject
    (
        "lm_b",
        runTime.timeName(),
        mesh,
        IOobject::MUST_READ,
        IOobject::AUTO_WRITE
    ),
    mesh
);

// Contact traction
surfaceVectorField tC =
(
    0.5*(t_M + t_P) + (0.5 * ((fvc::interpolate(Up)*nCn)
  + (fvc::interpolate(Us)*iMnCn)) & (lm_P - lm_M))
);

volVectorField t_b
(
    IOobject
    (
        "t_b",
        runTime.timeName(),
        mesh,
        IOobject::MUST_READ,
        IOobject::AUTO_WRITE
    ),
    mesh
);

// Riemann solver
#include "riemannSolver.H"


Info << "Creating fields for the constrained procedure ..." << endl;

// Constrained class
interpolationSchemes interpolate(mesh);

// Cell averaged linear momentum
volVectorField lmR = interpolate.surfaceToVol(lmC);

// Local gradient of cell averaged linear momentum
volTensorField lmRgrad = grad.localGradient(lmR, lmC);

// Constrained fluxes
interpolate.volToPoint(lmR, lmRgrad, lmN);
#include "strongBCs.H"
lmN.correctBoundaryConditions();
lmC = interpolate.pointToSurface(lmN);



Info << "Creating fields for angular momentum ..." << endl;

// Angular momentum class
angularMomentum am(mesh, mechanicalProperties);

// RHS of linear momentum equation
volVectorField rhsLm
(
    IOobject("rhsLm", mesh),
    mesh,
    dimensionedVector("rhsLm", dimensionSet(1,-2,-2,0,0,0,0), vector::zero)
);

// RHS of linear momentum equation (stage 1)
volVectorField rhsLm1 = rhsLm;

// RHS of angular momentum equation
volVectorField rhsAm
(
    IOobject("rhsAm", mesh),
    mesh,
    dimensionedVector("rhsAm", dimensionSet(1,-1,-2,0,0,0,0), vector::zero)
);


Info << "Creating fields for post-processing ..." << endl;

// Nodal displacements
pointVectorField uN
(
    IOobject
    (
        "uN",
        runTime.timeName(),
        mesh,
        IOobject::NO_READ,
        IOobject::AUTO_WRITE
    ),
    pMesh,
    dimensionedVector("uN", dimLength, vector::zero)
);
uN.write();


Info << "Creating variables for time ..." << endl;

// Time
dimensionedScalar t("t", dimTime, 0.0);

// Time step
scalar tstep = 0;

// Time increment
dimensionedScalar deltaT("deltaT", dimTime, runTime.deltaTValue());

if (timeStepping == "variable")
{
    deltaT = (cfl*h)/max(Up_time);
    runTime.setDeltaT(deltaT);
}

// Runge-Kutta stage
word RKstage("first");


Info << "Printing data ..." << endl;

// Print material properties
model.printMaterialProperties();

// Print global linear and angular momentum
am.printGlobalMomentum(lm,x);

// Print centroid of geometry
mech.printCentroid();