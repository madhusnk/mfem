//                       MFEM Example 21 - Parallel Version
//
// Compile with: make ex21p
//
// Sample runs:  mpirun -np 4 ex21p -m ../data/square-disc.mesh
//               mpirun -np 4 ex21p -m ../data/star.mesh
//               mpirun -np 4 ex21p -m ../data/escher.mesh
//               mpirun -np 4 ex21p -m ../data/fichera.mesh
//               mpirun -np 4 ex21p -m ../data/square-disc-p2.vtk -o 2
//               mpirun -np 4 ex21p -m ../data/square-disc-p3.mesh -o 3
//               mpirun -np 4 ex1p -m ../data/square-disc-nurbs.mesh -o -1
//               mpirun -np 4 ex1p -m ../data/disc-nurbs.mesh -o -1
//               mpirun -np 4 ex1p -m ../data/pipe-nurbs.mesh -o -1
//               mpirun -np 4 ex1p -m ../data/ball-nurbs.mesh -o 2
//               mpirun -np 4 ex1p -m ../data/star-surf.mesh
//               mpirun -np 4 ex1p -m ../data/square-disc-surf.mesh
//               mpirun -np 4 ex1p -m ../data/inline-segment.mesh
//               mpirun -np 4 ex1p -m ../data/amr-quad.mesh
//               mpirun -np 4 ex1p -m ../data/amr-hex.mesh
//               mpirun -np 4 ex1p -m ../data/mobius-strip.mesh
//               mpirun -np 4 ex1p -m ../data/mobius-strip.mesh -o -1 -sc
//
// Description:  This example code demonstrates the use of MFEM to define and
//               solve simple complex-valued linear systems.  We implement
//               three variants of a damped harmonic oscillator:
//
//               1) A scalar H1 field
//                  -Div(a Grad u) - omega^2 b u + i omega c u = 0
//
//               2) A vector H(Curl) field
//                  Curl(a Curl u) - omega^2 b u + i omega c u = 0
//
//               3) A vector H(Div) field
//                  -Grad(a Div u) - omega^2 b u + i omega c u = 0
//
//               In each case the field is driven by a forced oscillation, with
//               angular frequency omega, imposed at the boundary or a portion
//               of the boundary.
//
//               simple finite element discretization of the Laplace problem
//               -Delta u = 1 with homogeneous Dirichlet boundary conditions.
//               Specifically, we discretize using a FE space of the specified
//               order, or if order < 1 using an isoparametric/isogeometric
//               space (i.e. quadratic for quadratic curvilinear mesh, NURBS for
//               NURBS mesh, etc.)
//
//               The example highlights the use of mesh refinement, finite
//               element grid functions, as well as linear and bilinear forms
//               corresponding to the left-hand side and right-hand side of the
//               discrete linear system. We also cover the explicit elimination
//               of essential boundary conditions and the optional connection
//               to the GLVis tool for visualization.

#include "mfem.hpp"
#include <fstream>
#include <iostream>

using namespace std;
using namespace mfem;

static double mu_ = 1.0;
static double epsilon_ = 1.0;
static double sigma_ = 20.0;
static double omega_ = 10.0;

double u0_real_exact(const Vector &);
double u0_imag_exact(const Vector &);

void u1_real_exact(const Vector &, Vector &);
void u1_imag_exact(const Vector &, Vector &);

void u2_real_exact(const Vector &, Vector &);
void u2_imag_exact(const Vector &, Vector &);

bool check_for_inline_mesh(const char * mesh_file);

int main(int argc, char *argv[])
{
   // 1. Initialize MPI.
   int num_procs, myid;
   MPI_Init(&argc, &argv);
   MPI_Comm_size(MPI_COMM_WORLD, &num_procs);
   MPI_Comm_rank(MPI_COMM_WORLD, &myid);

   // 2. Parse command-line options.
   const char *mesh_file = "../data/inline-quad.mesh";
   int order = 1;
   int prob = 0;
   double freq = -1.0;
   bool visualization = 1;
   bool herm_conv = true;
   bool exact_sol = true;

   OptionsParser args(argc, argv);
   args.AddOption(&mesh_file, "-m", "--mesh",
                  "Mesh file to use.");
   args.AddOption(&order, "-o", "--order",
                  "Finite element order (polynomial degree).");
   args.AddOption(&prob, "-p", "--problem-type",
                  "Choose from 0: H_1, 1: H(Curl), or 2: H(Div) "
                  "damped harmonic oscillator.");
   args.AddOption(&mu_, "-mu", "--permeability",
                  "Permeability of free space (or 1/(spring constant)).");
   args.AddOption(&epsilon_, "-eps", "--permittivity",
                  "Permittivity of free space (or mass constant).");
   args.AddOption(&sigma_, "-sigma", "--conductivity",
                  "Conductivity (or damping constant).");
   args.AddOption(&freq, "-f", "--frequency",
                  "Frequency (in Hz).");
   args.AddOption(&herm_conv, "-herm", "--hermitian", "-no-herm",
                  "--no-hermitian", "Use convention for Hermitian operators.");
   args.AddOption(&visualization, "-vis", "--visualization", "-no-vis",
                  "--no-visualization",
                  "Enable or disable GLVis visualization.");
   args.Parse();
   if (!args.Good())
   {
      if (myid == 0)
      {
         args.PrintUsage(cout);
      }
      MPI_Finalize();
      return 1;
   }
   if (myid == 0)
   {
      args.PrintOptions(cout);
   }

   if ( freq > 0.0 )
   {
      omega_ = 2.0 * M_PI * freq;
   }

   exact_sol = check_for_inline_mesh(mesh_file);
   cout << "exact_sol set to " << exact_sol << endl;

   ComplexOperator::Convention conv =
      herm_conv ? ComplexOperator::HERMITIAN : ComplexOperator::BLOCK_SYMMETRIC;


   // 3. Read the (serial) mesh from the given mesh file on all processors.  We
   //    can handle triangular, quadrilateral, tetrahedral, hexahedral, surface
   //    and volume meshes with the same code.
   Mesh *mesh = new Mesh(mesh_file, 1, 1);
   int dim = mesh->Dimension();

   // 4. Refine the serial mesh on all processors to increase the resolution. In
   //    this example we do 'ref_levels' of uniform refinement. We choose
   //    'ref_levels' to be the largest number that gives a final mesh with no
   //    more than 10,000 elements.
   {
      int ref_levels = 1;
      //        (int)floor(log(10000./mesh->GetNE())/log(2.)/dim);
      for (int l = 0; l < ref_levels; l++)
      {
         mesh->UniformRefinement();
      }
   }

   // 5. Define a parallel mesh by a partitioning of the serial mesh. Refine
   //    this mesh further in parallel to increase the resolution. Once the
   //    parallel mesh is defined, the serial mesh can be deleted.
   ParMesh *pmesh = new ParMesh(MPI_COMM_WORLD, *mesh);
   delete mesh;
   {
      int par_ref_levels = 0;
      for (int l = 0; l < par_ref_levels; l++)
      {
         pmesh->UniformRefinement();
      }
   }

   // 6. Define a parallel finite element space on the parallel mesh. Here we
   //    use continuous Lagrange finite elements of the specified order. If
   //    order < 1, we instead use an isoparametric/isogeometric space.
   if (dim == 1 && prob != 0 )
   {
     if (myid == 0 )
     {
       cout << "Switching to problem type 0, H1 basis functions, "
	    << "for 1 dimensional mesh." << endl;
     }
     prob = 0;
   }

   FiniteElementCollection *fec;
   switch (prob)
   {
      case 0:
         fec = new H1_FECollection(order, dim);
         break;
      case 1:
         fec = new ND_FECollection(order, dim);
         break;
      case 2:
         fec = new RT_FECollection(order - 1, dim);
         break;
   }
   ParFiniteElementSpace *fespace = new ParFiniteElementSpace(pmesh, fec);
   HYPRE_Int size = fespace->GlobalTrueVSize();
   if (myid == 0)
   {
      cout << "Number of finite element unknowns: " << size << endl;
   }

   // 7. Determine the list of true (i.e. parallel conforming) essential
   //    boundary dofs. In this example, the boundary conditions are defined
   //    by marking all the boundary attributes from the mesh as essential
   //    (Dirichlet) and converting them to a list of true dofs.
   Array<int> ess_tdof_list;
   Array<int> ess_bdr;
   if (pmesh->bdr_attributes.Size())
   {
      ess_bdr.SetSize(pmesh->bdr_attributes.Max());
      ess_bdr = 1;
      if (exact_sol)
      {
         switch (prob)
         {
            case 0:
               ess_bdr = 0; ess_bdr[0] = 1;
               break;
            default:
               ess_bdr = 1; ess_bdr[2] = 0;
               break;
         }
      }
      fespace->GetEssentialTrueDofs(ess_bdr, ess_tdof_list);
   }

   // 8. Set up the parallel linear form b(.) which corresponds to the
   //    right-hand side of the FEM linear system.
   ParComplexLinearForm b(fespace, conv);
   b.Vector::operator=(0.0);

   // 9. Define the solution vector x as a parallel finite element grid function
   //    corresponding to fespace. Initialize x with initial guess of zero,
   //    which satisfies the boundary conditions.
   ParComplexGridFunction u(fespace);
   ParComplexGridFunction * u_exact = NULL;
   if (exact_sol) { u_exact = new ParComplexGridFunction(fespace); }

   FunctionCoefficient u0_r(u0_real_exact);
   FunctionCoefficient u0_i(u0_imag_exact);
   VectorFunctionCoefficient u1_r(dim, u1_real_exact);
   VectorFunctionCoefficient u1_i(dim, u1_imag_exact);
   VectorFunctionCoefficient u2_r(dim, u2_real_exact);
   VectorFunctionCoefficient u2_i(dim, u2_imag_exact);

   ConstantCoefficient zeroCoef(0.0);
   ConstantCoefficient oneCoef(1.0);

   Vector zeroVec(dim); zeroVec = 0.0;
   Vector  oneVec(dim);  oneVec = 0.0; oneVec[(prob==2)?(dim-1):0] = 1.0;
   VectorConstantCoefficient zeroVecCoef(zeroVec);
   VectorConstantCoefficient oneVecCoef(oneVec);

   switch (prob)
   {
      case 0:
         u.ProjectBdrCoefficient(oneCoef, zeroCoef, ess_bdr);
         if (exact_sol) { u_exact->ProjectCoefficient(u0_r, u0_i); }
         break;
      case 1:
         u.ProjectBdrCoefficientTangent(oneVecCoef, zeroVecCoef, ess_bdr);
         if (exact_sol) { u_exact->ProjectCoefficient(u1_r, u1_i); }
         break;
      case 2:
         u.ProjectBdrCoefficientNormal(oneVecCoef, zeroVecCoef, ess_bdr);
         if (exact_sol) { u_exact->ProjectCoefficient(u2_r, u2_i); }
         break;
   }

   if (visualization && exact_sol)
   {
      char vishost[] = "localhost";
      int  visport   = 19916;
      socketstream sol_sock_r(vishost, visport);
      socketstream sol_sock_i(vishost, visport);
      sol_sock_r << "parallel " << num_procs << " " << myid << "\n";
      sol_sock_i << "parallel " << num_procs << " " << myid << "\n";
      sol_sock_r.precision(8);
      sol_sock_i.precision(8);
      sol_sock_r << "solution\n" << *pmesh << u_exact->real()
                 << "window_title 'Exact Real Part'" << flush;
      sol_sock_i << "solution\n" << *pmesh << u_exact->imag()
                 << "window_title 'Exact Imaginary Part'" << flush;
   }

   // 10. Set up the parallel sesquilinear form a(.,.) on the finite element
   //     space corresponding to the damped harmonic oscillator operator
   //     -Div(tau Grad) + omega^2 rho - i omega sigma, by adding the
   //     Diffusion domain integrator and appropriate Mass domain integrators.

   ConstantCoefficient stiffnessCoef(1.0/mu_);
   ConstantCoefficient massCoef(-omega_ * omega_ * epsilon_);
   ConstantCoefficient lossCoef(omega_ * sigma_);
   ConstantCoefficient negMassCoef(omega_ * omega_ * epsilon_);

   ParSesquilinearForm *a = new ParSesquilinearForm(fespace, conv);
   switch (prob)
   {
      case 0:
         a->AddDomainIntegrator(new DiffusionIntegrator(stiffnessCoef),
                                NULL);
         a->AddDomainIntegrator(new MassIntegrator(massCoef),
                                new MassIntegrator(lossCoef));
         break;
      case 1:
         a->AddDomainIntegrator(new CurlCurlIntegrator(stiffnessCoef),
                                NULL);
         a->AddDomainIntegrator(new VectorFEMassIntegrator(massCoef),
                                new VectorFEMassIntegrator(lossCoef));
         break;
      case 2:
         a->AddDomainIntegrator(new DivDivIntegrator(stiffnessCoef),
                                NULL);
         a->AddDomainIntegrator(new VectorFEMassIntegrator(massCoef),
                                new VectorFEMassIntegrator(lossCoef));
         break;
   }

   // 10a. Set up the parallel bilinear form for the preconditioner
   //      corresponding to the operator
   //      -Div(tau Grad) + omega^2 rho + omega sigma
   ParBilinearForm *pcOp = new ParBilinearForm(fespace);
   switch (prob)
   {
      case 0:
         pcOp->AddDomainIntegrator(new DiffusionIntegrator(stiffnessCoef));
         pcOp->AddDomainIntegrator(new MassIntegrator(massCoef));
         pcOp->AddDomainIntegrator(new MassIntegrator(lossCoef));
         break;
      case 1:
         pcOp->AddDomainIntegrator(new CurlCurlIntegrator(stiffnessCoef));
         pcOp->AddDomainIntegrator(new VectorFEMassIntegrator(negMassCoef));
         pcOp->AddDomainIntegrator(new VectorFEMassIntegrator(lossCoef));
         break;
      case 2:
         pcOp->AddDomainIntegrator(new DivDivIntegrator(stiffnessCoef));
         pcOp->AddDomainIntegrator(new VectorFEMassIntegrator(massCoef));
         pcOp->AddDomainIntegrator(new VectorFEMassIntegrator(lossCoef));
         break;
   }

   // 11. Assemble the parallel bilinear form and the corresponding linear
   //     system, applying any necessary transformations such as: parallel
   //     assembly, eliminating boundary conditions, applying conforming
   //     constraints for non-conforming AMR, etc.
   a->Assemble();
   pcOp->Assemble();

   OperatorHandle A;
   Vector B, U;

   a->FormLinearSystem(ess_tdof_list, u, b, A, U, B);
   u = 0.0;
   U = 0.0;

   OperatorHandle PCOp;
   pcOp->FormSystemMatrix(ess_tdof_list, PCOp);

   if (myid == 0)
   {
      ComplexHypreParMatrix * Ahyp =
         dynamic_cast<ComplexHypreParMatrix*>(A.Ptr());

      cout << "Size of linear system: "
           << 2 * Ahyp->real().GetGlobalNumRows() << endl << endl;
   }

   // 12. Define and apply a parallel FGMRES solver for AX=B with the BoomerAMG
   //     preconditioner from hypre.
   {
      Array<HYPRE_Int> blockTrueOffsets;
      blockTrueOffsets.SetSize(3);
      blockTrueOffsets[0] = 0;
      blockTrueOffsets[1] = PCOp.Ptr()->Height();
      blockTrueOffsets[2] = PCOp.Ptr()->Height();
      blockTrueOffsets.PartialSum();

      BlockDiagonalPreconditioner BDP(blockTrueOffsets);

      Operator * pc_r = NULL;
      Operator * pc_i = NULL;

      switch (prob)
      {
         case 0:
            pc_r = new HypreBoomerAMG(dynamic_cast<HypreParMatrix&>(*PCOp.Ptr()));
            pc_i = new ScaledOperator(pc_r,
                                      (conv == ComplexOperator::HERMITIAN) ?
                                      1.0:-1.0);
            break;
         case 1:
            pc_r = new HypreAMS(dynamic_cast<HypreParMatrix&>(*PCOp.Ptr()),
                                fespace);
            pc_i = new ScaledOperator(pc_r,
                                      (conv == ComplexOperator::HERMITIAN) ?
                                      1.0:-1.0);
            break;
         case 2:
            if (dim == 2 )
            {
               pc_r = new HypreAMS(dynamic_cast<HypreParMatrix&>(*PCOp.Ptr()),
                                   fespace);
            }
            else
            {
               pc_r = new HypreADS(dynamic_cast<HypreParMatrix&>(*PCOp.Ptr()),
                                   fespace);
            }
            pc_i = new ScaledOperator(pc_r,
                                      (conv == ComplexOperator::HERMITIAN) ?
                                      1.0:-1.0);
            break;
      }
      BDP.SetDiagonalBlock(0, pc_r);
      BDP.SetDiagonalBlock(1, pc_i);
      BDP.owns_blocks = 0;

      FGMRESSolver fgmres(MPI_COMM_WORLD);
      fgmres.SetPreconditioner(BDP);
      fgmres.SetOperator(*A.Ptr());
      fgmres.SetRelTol(1e-12);
      fgmres.SetMaxIter(1000);
      fgmres.SetPrintLevel(1);
      fgmres.Mult(B, U);
   }

   // 13. Recover the parallel grid function corresponding to X. This is the
   //     local finite element solution on each processor.
   a->RecoverFEMSolution(U, b, u);

   if (exact_sol)
   {
      double err_r = -1.0;
      double err_i = -1.0;

      switch (prob)
      {
         case 0:
            err_r = u.real().ComputeL2Error(u0_r);
            err_i = u.imag().ComputeL2Error(u0_i);
            break;
         case 1:
            err_r = u.real().ComputeL2Error(u1_r);
            err_i = u.imag().ComputeL2Error(u1_i);
            break;
         case 2:
            err_r = u.real().ComputeL2Error(u2_r);
            err_i = u.imag().ComputeL2Error(u2_i);
            break;
      }

      if ( myid == 0 )
      {
         cout << endl;
         cout << "|| Re (u_h - u) ||_{L^2} = " << err_r << endl;
         cout << "|| Im (u_h - u) ||_{L^2} = " << err_i << endl;
         cout << endl;
      }
   }

   // 14. Save the refined mesh and the solution in parallel. This output can
   //     be viewed later using GLVis: "glvis -np <np> -m mesh -g sol".
   {
      ostringstream mesh_name, sol_r_name, sol_i_name;
      mesh_name << "mesh." << setfill('0') << setw(6) << myid;
      sol_r_name << "sol_r." << setfill('0') << setw(6) << myid;
      sol_i_name << "sol_i." << setfill('0') << setw(6) << myid;

      ofstream mesh_ofs(mesh_name.str().c_str());
      mesh_ofs.precision(8);
      pmesh->Print(mesh_ofs);

      ofstream sol_r_ofs(sol_r_name.str().c_str());
      ofstream sol_i_ofs(sol_i_name.str().c_str());
      sol_r_ofs.precision(8);
      sol_i_ofs.precision(8);
      u.real().Save(sol_r_ofs);
      u.imag().Save(sol_i_ofs);
   }

   // 15. Send the solution by socket to a GLVis server.
   if (visualization)
   {
      char vishost[] = "localhost";
      int  visport   = 19916;
      socketstream sol_sock_r(vishost, visport);
      socketstream sol_sock_i(vishost, visport);
      sol_sock_r << "parallel " << num_procs << " " << myid << "\n";
      sol_sock_i << "parallel " << num_procs << " " << myid << "\n";
      sol_sock_r.precision(8);
      sol_sock_i.precision(8);
      sol_sock_r << "solution\n" << *pmesh << u.real()
                 << "window_title 'Comp Real Part'" << flush;
      sol_sock_i << "solution\n" << *pmesh << u.imag()
                 << "window_title 'Comp Imaginary Part'" << flush;
   }
   if (visualization && exact_sol)
   {
      *u_exact -= u;

      char vishost[] = "localhost";
      int  visport   = 19916;
      socketstream sol_sock_r(vishost, visport);
      socketstream sol_sock_i(vishost, visport);
      sol_sock_r << "parallel " << num_procs << " " << myid << "\n";
      sol_sock_i << "parallel " << num_procs << " " << myid << "\n";
      sol_sock_r.precision(8);
      sol_sock_i.precision(8);
      sol_sock_r << "solution\n" << *pmesh << u_exact->real()
                 << "window_title 'Exact-Comp Real Part'" << flush;
      sol_sock_i << "solution\n" << *pmesh << u_exact->imag()
                 << "window_title 'Exact-Comp Imaginary Part'" << flush;
   }
   if (visualization)
   {
      ParGridFunction u_t(fespace);
      u_t = u.real();
      char vishost[] = "localhost";
      int  visport   = 19916;
      socketstream sol_sock(vishost, visport);
      sol_sock << "parallel " << num_procs << " " << myid << "\n";
      sol_sock.precision(8);
      sol_sock << "solution\n" << *pmesh << u_t
               << "window_title 'Harmonic Solution (t = 0.0 T)'"
               << "pause\n" << flush;
      if (myid == 0)
         cout << "GLVis visualization paused."
              << " Press space (in the GLVis window) to resume it.\n";
      int num_frames = 32;
      int i = 0;
      while (sol_sock)
      {
         double t = (double)(i % num_frames) / num_frames;
         ostringstream oss;
         oss << "Harmonic Solution (t = " << t << " T)";

         add(cos( 2.0 * M_PI * t), u.real(),
             sin(-2.0 * M_PI * t), u.imag(), u_t);
         sol_sock << "parallel " << num_procs << " " << myid << "\n";
         sol_sock << "solution\n" << *pmesh << u_t
                  << "window_title '" << oss.str() << "'" << flush;
         i++;
      }
   }

   // 16. Free the used memory.
   delete a;
   delete u_exact;
   delete pcOp;
   delete fespace;
   delete fec;
   delete pmesh;

   MPI_Finalize();

   return 0;
}

bool check_for_inline_mesh(const char * mesh_file)
{
   string file(mesh_file);
   size_t p0 = file.find_last_of("/");
   string s0 = file.substr((p0==string::npos)?0:(p0+1),7);
   return s0 == "inline-";
}

complex<double> u0_exact(const Vector &x)
{
   int dim = x.Size();
   complex<double> i(0.0, 1.0);
   complex<double> alpha = (epsilon_ * omega_ - i * sigma_);
   complex<double> kappa = std::sqrt(mu_ * omega_* alpha);
   return std::exp(-i * kappa * x[dim - 1]);
}

double u0_real_exact(const Vector &x)
{
   return u0_exact(x).real();
}

double u0_imag_exact(const Vector &x)
{
   return u0_exact(x).imag();
}

void u1_real_exact(const Vector &x, Vector &v)
{
   int dim = x.Size();
   v.SetSize(dim); v = 0.0; v[0] = u0_real_exact(x);
}

void u1_imag_exact(const Vector &x, Vector &v)
{
   int dim = x.Size();
   v.SetSize(dim); v = 0.0; v[0] = u0_imag_exact(x);
}

void u2_real_exact(const Vector &x, Vector &v)
{
   int dim = x.Size();
   v.SetSize(dim); v = 0.0; v[dim-1] = u0_real_exact(x);
}

void u2_imag_exact(const Vector &x, Vector &v)
{
   int dim = x.Size();
   v.SetSize(dim); v = 0.0; v[dim-1] = u0_imag_exact(x);
}
