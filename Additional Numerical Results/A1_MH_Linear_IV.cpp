#include <RcppArmadillo.h>
#include <math.h>
#include <iomanip>
#include <float.h>
#include <iostream>
#include <algorithm>
#include <random>

// [[Rcpp::depends(RcppArmadillo)]]
using namespace std;
using namespace Rcpp;


arma::vec logstar_arma(arma::vec X, double m, double thresh){
  arma::vec logX(m);
  for(int i=0; i<m; i++){
    if (X(i) < thresh) {
      logX(i) = log(thresh) - 1.5 + 2.0 * X(i)/thresh - 0.5 * pow((X(i)/thresh), 2);
    }
    else logX(i) = log(X(i));
  }
  return logX;
}


arma::vec derlogstar_arma(arma::vec X, double m, double thresh){
  arma::vec derlogX(m);
  for(int i=0; i<m; i++){
    if (X(i) < thresh) { 
      derlogX(i) = 2.0/thresh - X(i)/pow(thresh,2);
    }
    else derlogX(i) = 1.0/X(i);
  }
  return derlogX;
}


arma::vec der2logstar_arma(arma::vec X, double m, double thresh){
  arma::vec der2logX(m);
  for(int i=0; i<m; i++){
    if (X(i) < thresh) {
      der2logX(i) = -1.0/pow(thresh,2);
    }
    else der2logX(i) = -1.0/pow(X(i),2);
  }
  return der2logX;
}


// [[Rcpp::export]]
arma::mat IVfun_arma(arma::vec beta, arma::vec y, arma::mat x, arma::mat z){
  arma::vec residual = y - x * beta;
  arma::mat residual_mat = diagmat(residual);
  arma::mat Gee = residual_mat * z;
  return Gee; 
}


double U_beta_arma(arma::vec lambda, arma::mat Gee, double nu, double n, double thresh){
  arma::vec lam_g = Gee * lambda + 1.0;
  double obj = n * log(n) + arma::sum(logstar_arma(lam_g, n, thresh)) - n * nu * arma::norm(lambda,1);
  return obj; 
}


double U_pri_arma(arma::vec x, arma::vec mean, double sigma){
  double obj = pow(arma::norm((x - mean), 2), 2) / (2.0*pow(sigma,2));
  return obj; 
}


arma::vec Hessian_x(arma::vec dxu, arma::mat Gee, arma::mat Geet,
                    arma::vec d0, arma::vec d1, arma::vec d2){
  int r = (dxu.n_elem)/2;
  arma::vec x1 = dxu.head(r);
  arma::vec x2 = dxu.tail(r);
  arma::vec Hes_x(dxu.n_elem);
  Hes_x.head(r) = Geet*(d0%((Gee*x1)))+d1%x1+d2%x2;
  Hes_x.tail(r) = d2%x1+d1%x2;
  return Hes_x;
}



arma::mat Hessian(arma::mat Gee, arma::mat Geet,
                  arma::vec d0, arma::vec d1, arma::vec d2){
  int r = Gee.n_cols;
  
  arma::mat hessian(2*r,2*r);
  arma::mat D0 = arma::diagmat(d0);
  hessian.submat(0,0,r-1,r-1) = Geet*D0*Gee+arma::diagmat(d1);
  hessian.submat(0,r,r-1,2*r-1) = arma::diagmat(d2);
  hessian.submat(r,0,2*r-1,r-1) = arma::diagmat(d2);
  hessian.submat(r,r,2*r-1,2*r-1) = arma::diagmat(d1);
  
  return hessian;
}



arma::vec Pinv_x(arma::vec dxu,arma::vec p1,arma::vec p2,
                 arma::vec p3){
  int r = (dxu.n_elem)/2;
  arma::vec x1 = dxu.head(r);
  arma::vec x2 = dxu.tail(r);
  arma::vec Pr(dxu.n_elem);
  Pr.head(r) = p1%x1-p2%x2;
  Pr.tail(r) = -p2%x1+p3%x2;
  return Pr;
}



Rcpp::List pcg_(arma::vec dxu, arma::vec b, double tol, int maxit,
                arma::vec d0, arma::vec d1, arma::vec d2,
                arma::vec p1, arma::vec p2, arma::vec p3,
                arma::mat A, arma::mat At) {
  int n = dxu.n_elem;
  
  int iter,imin=0, flag=1;
  double normr,normr_act, normrmin, rho=1.0;
  double tolb, n2b = arma::norm(b,2);
  
  double rho1,beta,pq,alpha,relres;
  arma::vec x=dxu;
  
  arma::vec xmin=x, r, z, p, q;
  
  if(n2b==0){
    iter=0;
    flag=0;
    Rcout<<"n2b=0"<<endl;
    return List::create(Named("dlambda") = x, Named("iter") = iter, Named("flag")=flag);
  }
  
  tolb = tol*n2b;
  
  r = b - Hessian_x(x,A,At,d0,d1,d2);
  normr = arma::norm(r,2);
  normr_act = normr;
  if(normr<=tolb){
    flag=0;
    relres = normr/n2b;
    iter = 0;
    return List::create(Named("dlambda") = x, Named("iter") = iter, Named("flag")=flag);
  }
  
  
  normrmin = normr;
  int stag=0;
  int moresteps = 0;
  int maxstagsteps = 3;
  int maxmsteps = min(min(floor(n/50),5.0),double(n-maxit));
  int ii;
  bool verbose=false;
  if(verbose){
    Rcpp::Rcout<<n<<endl;
    Rcpp::Rcout<<n2b<<endl;
    Rcpp::Rcout<<x<<endl;
    Rcpp::Rcout<<r<<endl;
    Rcpp::Rcout<<tolb<<endl;
    Rcpp::Rcout<<maxmsteps<<endl;
    Rcpp::Rcout<<"true"<<endl;
  }
  
  for(int ii=1;ii<=maxit;ii++){
    z = Pinv_x(r, p1,p2,p3);
    if(!z.is_finite()){
      flag=2;
      break;
    }
    rho1 = rho;
    rho = arma::dot(r,z);
    if ((rho == 0) || std::isinf(rho))
    {flag = 4;
      break;}
    
    if(ii==1)
      p=z;
    else{
      beta = rho/rho1;
      if ((beta == 0) || std::isinf(beta))
      {flag = 4;
        break;}
      p = z+beta*p;
    }
    
    q = Hessian_x(p,A,At,d0,d1,d2);
    pq = arma::dot(p,q);
    
    if(pq<=0 || std::isinf(pq)){
      flag =4;
      break;
    }
    else
      alpha = rho/pq;
    
    if(std::isinf(alpha)){
      flag=4;
      break;
    }
    
    if((arma::norm(p,2)*abs(alpha))< ((2e-16)*arma::norm(x,2)))stag++;
    else stag = 0;
    
    x = x+alpha*p;
    r = r-alpha*q;
    normr = arma::norm(r,2);
    normr_act = normr;
    
    if(normr<=tolb || stag >=maxstagsteps || moresteps){
      r = b-Hessian_x(x,A,At,d0,d1,d2);
      normr_act = arma::norm(r,2);
      if(normr_act<=tolb){
        flag = 0;
        iter = ii;
        break;
      }
      else{
        if(stag>=maxstagsteps &&moresteps==0)stag=0;
        moresteps++;
        if(moresteps>=maxmsteps){
          flag = 3;
          iter=ii;
          break;
        }
      }
    }
    if (normr_act<normrmin){
      normrmin = normr_act;
      xmin = x;
      imin=ii;
    }
    if(stag>=maxstagsteps){flag=3;break;}
    
  }
  
  if(flag==0)relres = normr_act/n2b;
  else{
    arma::vec r_comp = b-Hessian_x(xmin,A,At,d0,d1,d2);
    if(arma::norm(r_comp,2)<=normr_act){
      x = xmin;
      iter = imin;
      relres = arma::norm(r_comp,2)/n2b;
    }
    else{
      iter = ii;
      relres = normr_act/n2b;
    }
  }
  return List::create(Named("dlambda") = x, Named("iter") = iter, Named("flag")=flag);
}


// [[Rcpp::export]]
arma::vec optimal_lam(arma::mat Gee, double nu,
                      double tar_gap, double eta = 1e-3, double pcgmaxi=5000,
                      bool verbose = false, string preconmat="identity"){
  
  // IPM PARAMETERS
  double MU = 2;         // updating parameter of t
  int MAX_NT_ITER= 400;     // maximum IPM (Newton) iteration
  
  // LINE SEARCH PARAMETERS
  double ALPHA           = 0.01;     // minimum fraction of decrease in the objective
  double BETA            = 0.5;      // stepsize decrease factor
  int MAX_LS_ITER     = 100;      // maximum backtracking line search iteration
  string status;
  
  int pitr = 0;  //pcg iter
  int pflag = 0 ; //pcg convergence
  
  int r = Gee.n_cols;
  double n = Gee.n_rows;
  
  arma::mat H,Geet = Gee.t();
  
  double t = min(max(1.0,1.0/nu),2*n/1e-3);
  double reltol = tar_gap;
  double thresh = 1.0/n;
  
  double pobj=1e100, dobj=-pobj, step = 1000;
  arma::vec lambda(r, arma::fill::ones);
  lambda = 0.1 * lambda;
  arma::vec u(r, arma::fill::ones);
  arma::vec f(r*2);
  f.head(r) = lambda-u;
  f.tail(r)= -lambda-u;
  int ntiter; //number of iteration
  int lsiter; //
  arma::vec d0;
  arma::vec z= Gee*lambda + 1.0;
  arma::vec dlambda_u(2*r, arma::fill::zeros);
  arma::vec diagxtx = 2.0*MU*arma::vec(r,arma::fill::ones);
  d0 = (-1.0/n)* der2logstar_arma(z, n,thresh);
  if (preconmat=="diag")
    arma::vec diagxtx = 2*arma::diagvec(Geet*diagmat(d0)*Gee);
  
  if(verbose==true){
    Rcpp::Rcout<<"Solving a proplem of size" <<n<<r<<", with nu="<<nu<<endl;
    Rcpp::Rcout<<"-------------------------------------------"<<endl;
    Rcpp::Rcout<<"iter  step len  pcgiters    gap          primobj       dualobj"<<endl;
    
  }
  
  //main loop
  double gap, normg, phi,newphi,gdx,pcgtol;
  arma::vec newlambda,newf(2*r),newz,newu,dlambda,du;
  
  arma::vec q1;
  arma::vec q2;
  arma::vec d1;
  arma::vec d2;
  arma::vec gradphi;
  arma::vec prb,prs;
  Rcpp::List result;
  arma::mat lower;
  arma::wall_clock timer;
  
  int bb=1;
  
  for (ntiter=0; ntiter<=MAX_NT_ITER; ntiter++){
    
    //arma::vec z= Gee*lambda + 1.0;
    z= Gee*lambda + 1.0;
    
    double s = min(nu * n / arma::norm(Geet * derlogstar_arma(z, n,thresh),"inf"), 1.0);
    arma::vec vv = s/n*derlogstar_arma(z,n,thresh);
    
    pobj = -1.0/n * arma::sum(logstar_arma(z, n, thresh))  + nu * arma::norm(lambda,1);
    dobj = max((arma::sum(log(n *vv)) / n + 1.0 - arma::sum(vv)), dobj);
    
    
    gap = pobj-dobj;
    
    if(verbose==true){
      Rcpp::Rcout<<" "<<ntiter<<"  ";
      Rcpp::Rcout<<step<<"     ";
      Rcpp::Rcout<<pitr<<"    ";
      Rcpp::Rcout<<setiosflags(ios::scientific)<<gap<<"  ";
      Rcpp::Rcout<<setiosflags(ios::scientific)<<pobj<<"  ";
      Rcpp::Rcout<<setiosflags(ios::scientific)<<dobj<<" ";
      Rcpp::Rcout<<" "<<MU<<endl;
    }
    if (gap/(-dobj)<reltol){
      status = "Solved";
      if(verbose==true)
        Rcpp::Rcout<<"Absolute tolerance reached"<<endl;
      return lambda;
    }
    
    if(step >= 0.5)
      t = max(8.0*min(2.0*n/gap,t),t);
    else if (step < 1e-5) {
      t = 1.5*t;
    }
    
    q1 = 1.0/(u+lambda);
    q2 = 1.0/(u-lambda);
    
    timer.tic();
    
    d0 = (-1.0/n)* der2logstar_arma(z, n,thresh);
    d1 = (q1%q1 + q2%q2)/t;
    d2 = (q1%q1 - q2%q2)/t;
    
    gradphi =  arma::join_vert((-1.0/n)*Geet*derlogstar_arma(z, n,thresh)-(q1-q2)/t,
                               nu*arma::vec(r,arma::fill::ones)-(q1+q2)/t);
    
    prb = diagxtx+d1;
    prs = prb%d1-(d2%d2);
    
    normg = arma::norm(gradphi,2);
    pcgtol = min(1e-1,eta*gap/min(1.0,normg));
    if (ntiter !=0 && pitr ==0)
      pcgtol = pcgtol*0.1;
    
    arma::vec tmp = dlambda_u;
    result = pcg_(dlambda_u,-gradphi,pcgtol,pcgmaxi,d0,d1,d2,d1/prs, d2/prs, prb/prs,Gee,Geet);
    
    arma::vec dlambda_u = result["dlambda"];
    if(dlambda_u.has_nan())Rcout<<"555"<<endl;
    pflag = result["flag"];
    pitr = result["iter"];
    
    if(pflag==1)pitr=pcgmaxi;
    
    dlambda = dlambda_u.head(r);
    du = dlambda_u.tail(r);
    
    phi = -1.0/n * arma::sum(logstar_arma(z, n,thresh)) + nu * arma::sum(u) - arma::sum(log(-f))/t;
    step = 1.00;
    gdx = arma::dot(gradphi,dlambda_u);
    for (lsiter = 1;lsiter<=MAX_LS_ITER;lsiter++){
      newlambda = lambda+step*dlambda; newu=u+step*du;
      newf.head(r) = newlambda-newu;
      newf.tail(r) = -1.0*newlambda-newu;
      if (arma::max(newf)<0){
        newz = Gee*newlambda + 1.0;
        
        newphi = -1.0/n * arma::sum(logstar_arma(newz, n,thresh))+nu*arma::sum(newu)-arma::sum(log(-newf))/t;
        if((newphi-phi)<=(ALPHA*step*gdx))
          break;
      }
      step=BETA*step;
    }
    
    if(lsiter == MAX_LS_ITER)break;
    
    lambda = newlambda; u = newu; f=newf;
  }
  
  if(lsiter == MAX_LS_ITER){
    Rcpp::Rcout<<"MAX_LS_ITER exceeded in BLS";
    status = "Failed";}
  else if(ntiter == MAX_NT_ITER)
    status = "Failed";
  return lambda;
}




// [[Rcpp::export]]
Rcpp::List MH_1_arma(arma::vec beta, arma::vec y, arma::mat x, arma::mat z, double nu){
  int p = beta.n_elem;
  double r = z.n_cols;
  double n = z.n_rows;
  double thresh = 1.0/n;
  int iter_sl = 3500;
  int iter;
  bool verbose=false;
  double sig = (3.5+(n-120)/80)/sqrt(n*log(r)), U_value, curr_U = 0.0, pro_U = 0.0, ratio, aplha, u, accept = 0.0, accept_rio = 0.0;
  arma::mat curr_Gee(n, r), pro_Gee(n, r), beta_iter(p, (iter_sl + 1), arma::fill::zeros);
  arma::vec lambda(r), curr_lambda(r), pro_lambda(r), curr_beta(p), pro_beta(p), beta_mean_3(p); 
  arma::vec mean_pri = {-1.0,-1.0};
  
  beta_iter.col(0) = beta;
  curr_beta = beta;
  for (iter=0; iter<iter_sl; iter++){
    curr_Gee = IVfun_arma(curr_beta, y, x, z);
    curr_lambda = optimal_lam(curr_Gee, nu, 1e-9, 1e-3, 5000, false, "identity");
    curr_U = U_beta_arma(curr_lambda, curr_Gee, nu, n, thresh) + U_pri_arma(curr_beta, mean_pri, 0.5);
    
    pro_beta = curr_beta + sig * arma::randn<arma::vec>(p);
    if (max(abs(pro_beta))<=10){
      pro_Gee = IVfun_arma(pro_beta, y, x, z);
      pro_lambda = optimal_lam(pro_Gee, nu, 1e-9, 1e-3, 5000, false, "identity");
      pro_U = U_beta_arma(pro_lambda, pro_Gee, nu, n, thresh) + U_pri_arma(pro_beta, mean_pri, 0.5);
      ratio = exp(-pro_U + curr_U);
    }
    
    else {
      ratio = 0.0;
    }
    
    // Metropolis Hastings step
    aplha = min(1.0, ratio);
    u = arma::randu<double>();
    
    if (u <= aplha){
      accept = accept + 1.0;
      curr_beta = pro_beta;
      lambda = pro_lambda;
      U_value = pro_U;
    }
    
    else{
      lambda = curr_lambda;
      U_value = curr_U;
    }
    
    beta_iter.col(1 + iter) = curr_beta;
  }
  
  beta_mean_3 = arma::mean(beta_iter.tail_cols(3000), 1);         // Mean_3: 501！！3500; 
  
  accept_rio = accept / iter_sl;
  
  return List::create(Named("beta_mean_3") = beta_mean_3,
                      Named("accept_rio") = accept_rio);
}



// [[Rcpp::export]]
Rcpp::List MH_2_arma(arma::vec beta, arma::vec y, arma::mat x, arma::mat z, double nu){
  int p = beta.n_elem;
  double r = z.n_cols;
  double n = z.n_rows;
  double thresh = 1.0/n;
  int iter_sl = 3500;
  int iter;
  bool verbose=false;
  double sig = (3.5+(n-120)/80)/sqrt(n*log(r)), U_value, curr_U = 0.0, pro_U = 0.0, ratio, aplha, u, accept = 0.0, accept_rio = 0.0;
  arma::mat curr_Gee(n, r), pro_Gee(n, r), beta_iter(p, (iter_sl + 1), arma::fill::zeros);
  arma::vec lambda(r), curr_lambda(r), pro_lambda(r), curr_beta(p), pro_beta(p), beta_mean_3(p); 
  arma::vec mean_pri = {0.6,0.6};
  
  beta_iter.col(0) = beta;
  curr_beta = beta;
  for (iter=0; iter<iter_sl; iter++){
    curr_Gee = IVfun_arma(curr_beta, y, x, z);
    curr_lambda = optimal_lam(curr_Gee, nu, 1e-9, 1e-3, 5000, false, "identity");
    curr_U = U_beta_arma(curr_lambda, curr_Gee, nu, n, thresh) + U_pri_arma(curr_beta, mean_pri, 0.5);
    
    pro_beta = curr_beta + sig * arma::randn<arma::vec>(p);
    if (max(abs(pro_beta))<=5){
      pro_Gee = IVfun_arma(pro_beta, y, x, z);
      pro_lambda = optimal_lam(pro_Gee, nu, 1e-9, 1e-3, 5000, false, "identity");
      pro_U = U_beta_arma(pro_lambda, pro_Gee, nu, n, thresh) + U_pri_arma(pro_beta, mean_pri, 0.5);
      ratio = exp(-pro_U + curr_U);
    }
    
    else {
      ratio = 0.0;
    }
    
    // Metropolis Hastings step
    aplha = min(1.0, ratio);
    u = arma::randu<double>();
    
    if (u <= aplha){
      accept = accept + 1.0;
      curr_beta = pro_beta;
      lambda = pro_lambda;
      U_value = pro_U;
    }
    
    else{
      lambda = curr_lambda;
      U_value = curr_U;
    }
    
    beta_iter.col(1 + iter) = curr_beta;
    
  }
  
  beta_mean_3 = arma::mean(beta_iter.tail_cols(3000), 1);         // Mean_3: 501！！3500; 
  
  accept_rio = accept / iter_sl;
  
  return List::create(Named("beta_mean_3") = beta_mean_3,
                      Named("accept_rio") = accept_rio);
}



// [[Rcpp::export]]
Rcpp::List MH_3_arma(arma::vec beta, arma::vec y, arma::mat x, arma::mat z, double nu){
  int p = beta.n_elem;
  double r = z.n_cols;
  double n = z.n_rows;
  double thresh = 1.0/n;
  int iter_sl = 3500;
  int iter;
  bool verbose=false;
  double sig = (3.5+(n-120)/80)/sqrt(n*log(r)), U_value, curr_U = 0.0, pro_U = 0.0, ratio, aplha, u, accept = 0.0, accept_rio = 0.0;
  arma::mat curr_Gee(n, r), pro_Gee(n, r), beta_iter(p, (iter_sl + 1), arma::fill::zeros);
  arma::vec lambda(r), curr_lambda(r), pro_lambda(r), curr_beta(p), pro_beta(p), beta_mean_3(p); 
  
  beta_iter.col(0) = beta;
  curr_beta = beta;
  for (iter=0; iter<iter_sl; iter++){
    curr_Gee = IVfun_arma(curr_beta, y, x, z);
    curr_lambda = optimal_lam(curr_Gee, nu, 1e-9, 1e-3, 5000, false, "identity");
    curr_U = U_beta_arma(curr_lambda, curr_Gee, nu, n, thresh);
    
    pro_beta = curr_beta + sig * arma::randn<arma::vec>(p);
    if (max(abs(pro_beta))<=5){
      pro_Gee = IVfun_arma(pro_beta, y, x, z);
      pro_lambda = optimal_lam(pro_Gee, nu, 1e-9, 1e-3, 5000, false, "identity");
      pro_U = U_beta_arma(pro_lambda, pro_Gee, nu, n, thresh);
      ratio = exp(-pro_U + curr_U);
    }
    
    else {
      ratio = 0.0;
    }
    
    // Metropolis Hastings step
    aplha = min(1.0, ratio);
    u = arma::randu<double>();
    
    if (u <= aplha){
      accept = accept + 1.0;
      curr_beta = pro_beta;
      lambda = pro_lambda;
      U_value = pro_U;
    }
    
    else{
      lambda = curr_lambda;
      U_value = curr_U;
    }
    
    beta_iter.col(1 + iter) = curr_beta;
  }
  
  beta_mean_3 = arma::mean(beta_iter.tail_cols(3000), 1);         // Mean_3: 501！！3500; 
  
  accept_rio = accept / iter_sl;
  
  return List::create(Named("beta_mean_3") = beta_mean_3,
                      Named("accept_rio") = accept_rio);
}


