#ifndef CURVEFITTING_RAL_NLLS_INTERNAL_H_
#define CURVEFITTING_RAL_NLLS_INTERNAL_H_

#include "MantidCurveFitting/DllConfig.h"
#include "MantidCurveFitting/FortranDefs.h"

#include <limits>
#include <functional>

namespace Mantid {
namespace CurveFitting {
namespace RalNlls {

typedef double real;
typedef double REAL;
typedef int integer;
typedef int INTEGER;
typedef bool logical;
typedef bool LOGICAL;

namespace {

const double tenm3 = 1.0e-3;
const double tenm5 = 1.0e-5;
const double tenm8 = 1.0e-8;
const double hundred = 100.0;
const double ten = 10.0;
const double point9 = 0.9;
const double zero = 0.0;
const double one = 1.0;
const double two = 2.0;
const double half = 0.5;
const double sixteenth = 0.0625;

}

// deliberately empty
typedef void* params_base_type;

//  abstract interface
//     subroutine eval_f_type(status, n, m, x, f, params)
//       import  params_base_type
//       implicit none
//       integer, intent(out)  status
//       integer, intent(in)  n,m 
//       double precision, dimension(*), intent(in)   x
//       double precision, dimension(*), intent(out)  f
//       class(params_base_type), intent(in)  params
//     end subroutine eval_f_type
//  end interface
typedef std::function<void(int &status, int n, int m,
  const DoubleFortranVector &x, DoubleFortranVector &f,
  params_base_type params)> eval_f_type;

//  abstract interface
//     subroutine eval_j_type(status, n, m, x, J, params)
//       import  params_base_type
//       implicit none
//       integer, intent(out)  status
//       integer, intent(in)  n,m 
//       double precision, dimension(*), intent(in)   x
//       double precision, dimension(*), intent(out)  J
//       class(params_base_type), intent(in)  params
//     end subroutine eval_j_type
//  end interface
typedef std::function<void(int &status, int n, int m,
  const DoubleFortranVector &x, DoubleFortranMatrix &J,
  params_base_type params)> eval_j_type;

//  abstract interface
//     subroutine eval_hf_type(status, n, m, x, f, h, params)
//       import  params_base_type
//       implicit none
//       integer, intent(out)  status
//       integer, intent(in)  n,m 
//       double precision, dimension(*), intent(in)   x
//       double precision, dimension(*), intent(in)   f
//       double precision, dimension(*), intent(out)  h
//       class(params_base_type), intent(in)  params
//     end subroutine eval_hf_type
//  end interface
typedef std::function<void(int &status, int n, int m,
  const DoubleFortranVector &x,
  const DoubleFortranVector &f, DoubleFortranMatrix &h,
  params_base_type params)> eval_hf_type;


enum class NLLS_ERROR {
  OK = 0,
  MAXITS = -1,
  EVALUATION = -2,
  UNSUPPORTED_MODEL = -3,
  FROM_EXTERNAL = -4,
  UNSUPPORTED_METHOD = -5,
  ALLOCATION = -6,
  MAX_TR_REDUCTIONS = -7,
  X_NO_PROGRESS = -8,
  N_GT_M = -9,
  BAD_TR_STRATEGY = -10,
  FIND_BETA = -11,
  BAD_SCALING = -12,
  //     ! dogleg errors
  DOGLEG_MODEL = -101,
  //     ! AINT errors
  AINT_EIG_IMAG = -201,
  AINT_EIG_ODD = -202,
  //     ! More-Sorensen errors
  MS_MAXITS = -301,
  MS_TOO_MANY_SHIFTS = -302,
  MS_NO_PROGRESS = -303
  //     ! DTRS errors
};

struct nlls_options {

  //!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
  //!!! M A I N   R O U T I N E   C O N T R O L S !!!
  //!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

  //!   the maximum number of iterations performed
  int maxit = 100;

  //!   specify the model used. Possible values are
  //!
  //!      0  dynamic (*not yet implemented*)
  //!      1  Gauss-Newton (no 2nd derivatives)
  //!      2  second-order (exact Hessian)
  //!      3  hybrid (using Madsen, Nielsen and Tingleff's method)    
  int model = 3;

  //!   specify the method used to solve the trust-region sub problem
  //!      1 Powell's dogleg
  //!      2 AINT method (of Yuji Nat.)
  //!      3 More-Sorensen
  //!      4 Galahad's DTRS
  int nlls_method = 4;

  //!  which linear least squares solver should we use?
  int lls_solver = 1;

  //!   overall convergence tolerances. The iteration will terminate when the
  //!     norm of the gradient of the objective function is smaller than 
  //!       MAX( .stop_g_absolute, .stop_g_relative * norm of the initial gradient
  //!     or if the step is less than .stop_s
  double stop_g_absolute = tenm5;
  double stop_g_relative = tenm8;

  //!   should we scale the initial trust region radius?
  int relative_tr_radius = 0;

  //!   if relative_tr_radius == 1, then pick a scaling parameter
  //!   Madsen, Nielsen and Tingleff say pick this to be 1e-6, say, if x_0 is good,
  //!   otherwise 1e-3 or even 1 would be good starts...
  double initial_radius_scale = 1.0;

  //!   if relative_tr_radius /= 1, then set the 
  //!   initial value for the trust-region radius (-ve => ||g_0||)
  double initial_radius = hundred;

  //!   maximum permitted trust-region radius
  double maximum_radius = 1.0e8; //ten ** 8

                                 //!   a potential iterate will only be accepted if the actual decrease
                                 //!    f - f(x_new) is larger than .eta_successful times that predicted
                                 //!    by a quadratic model of the decrease. The trust-region radius will be
                                 //!    increased if this relative decrease is greater than .eta_very_successful
                                 //!    but smaller than .eta_too_successful
  double eta_successful = 1.0e-8; // ten ** ( - 8 )
  double eta_success_but_reduce = 1.0e-8; // ten ** ( - 8 )
  double eta_very_successful = point9;
  double eta_too_successful = two;

  //!   on very successful iterations, the trust-region radius will be increased by
  //!    the factor .radius_increase, while if the iteration is unsuccessful, the 
  //!    radius will be decreased by a factor .radius_reduce but no more than
  //!    .radius_reduce_max
  double radius_increase = two;
  double radius_reduce = half;
  double radius_reduce_max = sixteenth;

  //! Trust region update strategy
  //!    1 - usual step function
  //!    2 - continuous method of Hans Bruun Nielsen (IMM-REP-1999-05)
  int tr_update_strategy = 1;

  //!   if model=7, then the value with which we switch on second derivatives
  double hybrid_switch = 0.1;

  //!   shall we use explicit second derivatives, or approximate using a secant 
  //!   method
  bool exact_second_derivatives = false;

  //!   use a factorization (dsyev) to find the smallest eigenvalue for the subproblem
  //!    solve? (alternative is an iterative method (dsyevx)
  bool subproblem_eig_fact = false; //! undocumented....

                                    //!   scale the variables?
                                    //!   0 - no scaling
                                    //!   1 - use the scaling in GSL (W s.t. W_ii = ||J(i,:)||_2^2)
                                    //!       tiny values get set to one       
                                    //!   2 - scale using the approx to the Hessian (W s.t. W = ||H(i,:)||_2^2
  int scale = 1;
  double scale_max = 1e11;
  double scale_min = 1e-11;
  bool scale_trim_min = true;
  bool scale_trim_max = true;
  bool scale_require_increase = false;
  bool calculate_svd_J = true;

  //!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
  //!!! M O R E - S O R E N S E N   C O N T R O L S !!!
  //!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!     
  int more_sorensen_maxits = 500;
  real more_sorensen_shift = 1e-13;
  real  more_sorensen_tiny = 10.0 * epsmch;
  real  more_sorensen_tol = 1e-3;

  //!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
  //!!! H Y B R I D   C O N T R O L S !!!
  //!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

  //! what's the tolerance such that ||J^T f || < tol * 0.5 ||f||^2 triggers a switch
  real  hybrid_tol = 2.0;

  //! how many successive iterations does the above condition need to hold before we switch?
  integer   hybrid_switch_its = 1;

  //!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
  //!!! O U T P U T   C O N T R O L S !!!
  //!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

  //! Shall we output progess vectors at termination of the routine?
  logical  output_progress_vectors = false;

}; // struct nlls_options

   //!  - - - - - - - - - - - - - - - - - - - - - - - 
   //!   inform derived type with component defaults
   //!  - - - - - - - - - - - - - - - - - - - - - - - 
struct  nlls_inform {

  //!  return status
  //!  (see ERROR type for descriptions)
  NLLS_ERROR  status = NLLS_ERROR::OK;

  //! error message     
  //     CHARACTER ( LEN = 80 )  error_message = REPEAT( ' ', 80 )
  std::string error_message;

  //!  the status of the last attempted allocation/deallocation
  INTEGER  alloc_status = 0;

  //!  the name of the array for which an allocation/deallocation error ocurred
  //     CHARACTER ( LEN = 80 )  bad_alloc = REPEAT( ' ', 80 )
  std::string bad_alloc;

  //!  the total number of iterations performed
  INTEGER  iter;

  //!  the total number of CG iterations performed
  //!$$     INTEGER  cg_iter = 0

  //!  the total number of evaluations of the objective function
  INTEGER  f_eval = 0;

  //!  the total number of evaluations of the gradient of the objective function
  INTEGER  g_eval = 0;

  //!  the total number of evaluations of the Hessian of the objective function
  INTEGER  h_eval = 0;

  //!  test on the size of f satisfied?
  integer  convergence_normf = 0;

  //!  test on the size of the gradient satisfied?
  integer  convergence_normg = 0;

  //!  vector of residuals 
  //     real, allocatable  resvec(:)
  DoubleFortranVector resvec;

  //!  vector of gradients 
  //     real, allocatable  gradvec(:)
  DoubleFortranVector gradvec;

  //!  vector of smallest singular values
  //     real, allocatable  smallest_sv(:)
  DoubleFortranVector smallest_sv;

  //!  vector of largest singular values
  //     real, allocatable  largest_sv(:)
  DoubleFortranVector largest_sv;

  //!  the value of the objective function at the best estimate of the solution 
  //!   determined by NLLS_solve
  REAL obj = HUGE;

  //!  the norm of the gradient of the objective function at the best estimate 
  //!   of the solution determined by NLLS_solve
  REAL norm_g = HUGE;

  //! the norm of the gradient, scaled by the norm of the residual
  REAL scaled_g = HUGE;

  //! error returns from external subroutines 
  INTEGER  external_return = 0;

  //! name of external program that threw and error
  //     CHARACTER ( LEN = 80 )  external_name = REPEAT( ' ', 80 )
  std::string external_name;

}; //  END TYPE nlls_inform

   //  ! define types for workspace arrays.

   //! workspace for subroutine max_eig
struct max_eig_work {
  DoubleFortranVector alphaR, alphaI, beta;
  DoubleFortranMatrix vr;
  DoubleFortranVector work, ew_array;
  IntFortranVector nullindex;
  IntFortranVector vecisreal;  // logical, allocatable  vecisreal(:)
  integer nullevs_cols;
  DoubleFortranMatrix nullevs;
};

// ! workspace for subroutine solve_general
struct solve_general_work {
  DoubleFortranMatrix A;
  IntFortranVector ipiv;
};

//! workspace for subroutine evaluate_model
struct evaluate_model_work {
  DoubleFortranVector Jd, Hd;
};

//! workspace for subroutine solve_LLS
struct solve_LLS_work {
  DoubleFortranVector temp, work;
  DoubleFortranMatrix Jlls;
};

//! workspace for subroutine min_eig_work
struct min_eig_symm_work {
  DoubleFortranMatrix A;
  DoubleFortranVector work, ew;
  IntFortranVector iwork, ifail;
};

//! workspace for subroutine all_eig_symm
struct all_eig_symm_work {
  DoubleFortranVector work;
};

//! workspace for subrouine apply_scaling
struct apply_scaling_work {
  DoubleFortranVector diag;
  DoubleFortranMatrix ev;
  DoubleFortranVector tempvec;
  all_eig_symm_work all_eig_symm_ws;
};

//! workspace for subroutine dtrs_work
struct solve_dtrs_work {
  DoubleFortranMatrix A, ev;
  DoubleFortranVector ew, v, v_trans, d_trans;
  all_eig_symm_work all_eig_symm_ws;
  apply_scaling_work apply_scaling_ws;
};

//! workspace for subroutine more_sorensen
struct more_sorensen_work {
  //!      type( solve_spd_work )  solve_spd_ws
  DoubleFortranMatrix A, LtL, AplusSigma;
  DoubleFortranVector v, q, y1;
  //!       type( solve_general_work )  solve_general_ws
  min_eig_symm_work min_eig_symm_ws;
  apply_scaling_work apply_scaling_ws;
};

//! workspace for subroutine calculate_step
struct calculate_step_work {
  more_sorensen_work more_sorensen_ws;
  solve_dtrs_work solve_dtrs_ws;
  //calculate_step_work(int n, int m, int nlls_method);
};

//! workspace for subroutine get_svd_J
struct get_svd_J_work {
  DoubleFortranVector Jcopy, S, work;       
  //get_svd_J_work(int n, int m, bool calculate_svd_J);
};

//! all workspaces called from the top level
struct NLLS_workspace {
  integer  first_call = 1;
  integer  iter = 0;
  real  normF0, normJF0, normF, normJF;
  real  normJFold, normJF_Newton;
  real  Delta;
  real  normd;
  logical  use_second_derivatives = false;
  integer  hybrid_count = 0;
  real  hybrid_tol = 1.0;
  real  tr_nu = 2.0;
  integer  tr_p = 3;
  DoubleFortranMatrix fNewton, JNewton, XNewton;
  DoubleFortranMatrix J;
  DoubleFortranVector f, fnew;
  DoubleFortranMatrix hf, hf_temp;
  DoubleFortranVector d, g, Xnew;
  DoubleFortranVector y, y_sharp, g_old, g_mixed;
  DoubleFortranVector ysharpSks, Sks;
  DoubleFortranVector resvec, gradvec;
  DoubleFortranVector largest_sv, smallest_sv;
  get_svd_J_work get_svd_J_ws;
  calculate_step_work calculate_step_ws;
  evaluate_model_work evaluate_model_ws;
  NLLS_workspace(int n, int m,const nlls_options& options, nlls_inform& inform);
};

///  Given an (m x n)  matrix J, this routine returns the largest 
///  and smallest singular values of J.
void get_svd_J(const DoubleFortranMatrix& J, double &s1, double &sn);

/// Calculate the 2-norm of a vector: sqrt(||V||^2)
double norm2(const DoubleFortranVector& v);

void mult_J(const DoubleFortranMatrix& J, const DoubleFortranVector& x, DoubleFortranVector& Jx);
void mult_Jt(const DoubleFortranMatrix& J, const DoubleFortranVector& x, DoubleFortranVector& Jtx);
void calculate_step(const DoubleFortranMatrix& J, const DoubleFortranVector& f, const DoubleFortranMatrix& hf, const DoubleFortranVector& g,
  int n, int m, double Delta, DoubleFortranVector& d, double& normd, const nlls_options& options, nlls_inform& inform, calculate_step_work& w);
void evaluate_model(const DoubleFortranVector& f, const DoubleFortranMatrix& J, const DoubleFortranMatrix& hf,
  const DoubleFortranVector& d, double& md, int m, int n, const nlls_options options, evaluate_model_work& w);
void calculate_rho(double normf, double normfnew,double md, double& rho, const nlls_options& options);
void update_trust_region_radius(double& rho, const nlls_options& options, nlls_inform& inform, NLLS_workspace& w);
void rank_one_update(DoubleFortranMatrix& hf, NLLS_workspace w, int n);
void apply_second_order_info(int n, int m, const DoubleFortranVector& X,
  NLLS_workspace& w, eval_hf_type eval_HF, params_base_type params, 
  const nlls_options& options, nlls_inform& inform, const DoubleFortranVector& weights);
void test_convergence(double normF, double normJF, double normF0, double normJF0,
  const nlls_options& options, nlls_inform& inform);

} // RalNlls
} // CurveFitting
} // Mantid

#endif // CURVEFITTING_RAL_NLLS_INTERNAL_H_
