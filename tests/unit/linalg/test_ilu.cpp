// Copyright (c) 2019, Lawrence Livermore National Security, LLC. Produced at
// the Lawrence Livermore National Laboratory. LLNL-CODE-443211. All Rights
// reserved. See file COPYRIGHT for details.
//
// This file is part of the MFEM library. For more information and source code
// availability see http://mfem.org.
//
// MFEM is free software; you can redistribute it and/or modify it under the
// terms of the GNU Lesser General Public License (as published by the Free
// Software Foundation) version 2.1 dated February 1999.

#include "catch.hpp"
#include "mfem.hpp"

using namespace mfem;

TEST_CASE("ILU Structure", "[ILU]")
{
   int N = 5;
   int Nb = 3;
   int nnz_blocks = 11;

   // Submatrix of sie Nb x Nb
   DenseMatrix Ab(Nb, Nb);

   // Matrix with N x N blocks of size Nb x Nb
   SparseMatrix A(N * Nb, N * Nb);
   /** Create a SparseMatrix that has a block structure looking like

      {{1, 1, 0, 0, 1},
       {0, 1, 0, 1, 1},
       {0, 0, 1, 0, 0},
       {0, 1, 0, 1, 0},
       {1, 0, 0, 0, 1}}

      Where 1 represents a block of size Nb x Nb that is non zero.
  */

   // Lexographical pattern
   int p[] = {1, 1, 0, 0, 1, 0, 1, 0, 1, 1, 0, 0, 1,
              0, 0, 0, 1, 0, 1, 0, 1, 0, 0, 0, 1};

   Array<int> pattern(p, N * N);
   int counter = 1;
   for (int i = 0; i < N; ++i)
   {
      for (int j = 0; j < N; ++j)
      {
         if (pattern[N * i + j] == 1)
         {
            Array<int> rows, cols;

            for (int ii = 0; ii < Nb; ++ii)
            {
               rows.Append(i * Nb + ii);
               cols.Append(j * Nb + ii);
            }

            Vector Ab_data(Ab.GetData(), Nb * Nb);
            Ab_data.Randomize(counter);
            A.SetSubMatrix(rows, cols, Ab);
         }
      }
   }

   A.Finalize();

   SECTION("Create block pattern from SparseMatrix")
   {
      BlockILU0 ilu(&A, Nb);

      int nnz_count = 0;

      for (int i = 0; i < N; ++i)
      {
         for (int k = ilu.IB[i]; k < ilu.IB[i + 1]; ++k)
         {
            int j = ilu.JB[k];
            // Check if the non zero block is expected
            REQUIRE(pattern[i * N + j] == 1);
            // Check that the block data is the same
            // for (int bi = 0; bi < Nb; ++bi)
            // {
            //    for (int bj = 0; bj < Nb; ++bj)
            //    {
            //       REQUIRE(ilu.AB[bi + bj * Nb + k * Nb * Nb]
            //               == A(i * Nb + bi, j * Nb + bj));
            //    }
            // }
            nnz_count++;
         }
      }

      // Check if the number of expected non zero blocks matches
      REQUIRE(nnz_count == nnz_blocks);
   }
}


TEST_CASE("ILU Factorization", "[ILU]")
{
   SparseMatrix A(6, 6);

   A.Set(0,0,1);
   A.Set(0,1,2);
   A.Set(0,2,3);
   A.Set(0,3,4);
   A.Set(0,4,5);
   A.Set(0,5,6);

   A.Set(1,0,7);
   A.Set(1,1,8);
   A.Set(1,2,9);
   A.Set(1,3,1);
   A.Set(1,4,2);
   A.Set(1,5,3);

   A.Set(2,0,4);
   A.Set(2,1,5);
   A.Set(2,2,6);
   A.Set(2,3,7);

   A.Set(3,0,8);
   A.Set(3,1,9);
   A.Set(3,2,1);
   A.Set(3,3,2);

   A.Set(4,0,3);
   A.Set(4,1,4);
   A.Set(4,4,5);
   A.Set(4,5,6);

   A.Set(5,0,7);
   A.Set(5,1,8);
   A.Set(5,4,9);
   A.Set(5,5,1);

   A.Finalize();

   BlockILU0 ilu(&A, 2);

   REQUIRE(ilu.AB[0 + 0*2 + 0*4] == Approx(1.0));
   REQUIRE(ilu.AB[1 + 0*2 + 0*4] == Approx(7.0));
   REQUIRE(ilu.AB[0 + 1*2 + 0*4] == Approx(2.0));
   REQUIRE(ilu.AB[1 + 1*2 + 0*4] == Approx(8.0));

   REQUIRE(ilu.AB[0 + 0*2 + 1*4] == Approx(3.0));
   REQUIRE(ilu.AB[1 + 0*2 + 1*4] == Approx(9.0));
   REQUIRE(ilu.AB[0 + 1*2 + 1*4] == Approx(4.0));
   REQUIRE(ilu.AB[1 + 1*2 + 1*4] == Approx(1.0));

   REQUIRE(ilu.AB[0 + 0*2 + 2*4] == Approx(5.0));
   REQUIRE(ilu.AB[1 + 0*2 + 2*4] == Approx(2.0));
   REQUIRE(ilu.AB[0 + 1*2 + 2*4] == Approx(6.0));
   REQUIRE(ilu.AB[1 + 1*2 + 2*4] == Approx(3.0));

   REQUIRE(ilu.AB[0 + 0*2 + 3*4] == Approx(1.0/2.0));
   REQUIRE(ilu.AB[1 + 0*2 + 3*4] == Approx(-1.0/6.0));
   REQUIRE(ilu.AB[0 + 1*2 + 3*4] == Approx(1.0/2.0));
   REQUIRE(ilu.AB[1 + 1*2 + 3*4] == Approx(7.0/6.0));

   REQUIRE(ilu.AB[0 + 0*2 + 4*4] == Approx(0.0));
   REQUIRE(ilu.AB[1 + 0*2 + 4*4] == Approx(-9.0));
   REQUIRE(ilu.AB[0 + 1*2 + 4*4] == Approx(4.5));
   REQUIRE(ilu.AB[1 + 1*2 + 4*4] == Approx(1.5));

   REQUIRE(ilu.AB[0 + 0*2 + 5*4] == Approx(2.0/3.0));
   REQUIRE(ilu.AB[1 + 0*2 + 5*4] == Approx(0.0));
   REQUIRE(ilu.AB[0 + 1*2 + 5*4] == Approx(1.0/3.0));
   REQUIRE(ilu.AB[1 + 1*2 + 5*4] == Approx(1.0));

   REQUIRE(ilu.AB[0 + 0*2 + 6*4] == Approx(1.0));
   REQUIRE(ilu.AB[1 + 0*2 + 6*4] == Approx(7.0));
   REQUIRE(ilu.AB[0 + 1*2 + 6*4] == Approx(1.0));
   REQUIRE(ilu.AB[1 + 1*2 + 6*4] == Approx(-2.0));
}