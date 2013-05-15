/*
	MCMC and ALS based learning for factorization machines
	- this file contains the sampler for a full sample of all model and prior parameters 

	Based on the publication(s):
	* Steffen Rendle (2010): Factorization Machines, in Proceedings of the 10th IEEE International Conference on Data Mining (ICDM 2010), Sydney, Australia.
	* Steffen Rendle, Zeno Gantner, Christoph Freudenthaler, Lars Schmidt-Thieme (2011): Fast Context-aware Recommendations with Factorization Machines, in Proceedings of the 34th international ACM SIGIR conference on Research and development in information retrieval (SIGIR 2011), Beijing, China.
	* Christoph Freudenthaler, Lars Schmidt-Thieme, Steffen Rendle (2011): Bayesian Factorization Machines, in NIPS Workshop on Sparse Representation and Low-rank Approximation (NIPS-WS 2011), Spain.
	* Steffen Rendle (2012): Factorization Machines with libFM, ACM Transactions on Intelligent Systems and Technology (TIST 2012).

	Author:   Steffen Rendle, http://www.libfm.org/
	modified: 2012-12-27

	Copyright 2010-2012 Steffen Rendle, see license.txt for more information
*/


#ifndef FM_LEARN_MCMC_H_
#define FM_LEARN_MCMC_H_

#include <sstream>


struct e_q_term {
	double e;
	double q;
};


class fm_learn_mcmc : public fm_learn {
	public:
		virtual double evaluate(Data& data) { return std::numeric_limits<double>::quiet_NaN(); }
	protected:
		virtual double predict_case(Data& data) {
			throw "not supported for MCMC and ALS";
		}
	public:
		uint num_iter;
		uint num_eval_cases;

		double alpha_0, gamma_0, beta_0, mu_0;
		double alpha;
		
		double w0_mean_0;
 
		DVector<double> w_mu, w_lambda;

		DMatrix<double> v_mu, v_lambda;


		bool do_sample; // switch between choosing expected values and drawing from distribution 
		bool do_multilevel; // use the two-level (hierarchical) model (TRUE) or the one-level (FALSE)
		uint nan_cntr_v, nan_cntr_w, nan_cntr_w0, nan_cntr_alpha, nan_cntr_w_mu, nan_cntr_w_lambda, nan_cntr_v_mu, nan_cntr_v_lambda;
		uint inf_cntr_v, inf_cntr_w, inf_cntr_w0, inf_cntr_alpha, inf_cntr_w_mu, inf_cntr_w_lambda, inf_cntr_v_mu, inf_cntr_v_lambda;

	protected:
		DVector<double> cache_for_group_values;

		DVector<double> pred_sum_all;
		DVector<double> pred_sum_all_but5;
		DVector<double> pred_this;

		e_q_term* cache;
		e_q_term* cache_test;

		sparse_row<DATA_FLOAT> empty_data_row; // this is a dummy row for attributes that do not exist in the training data (but in test data)		

		virtual void _learn(Data& train, Data& test) {};
	

		/**
			This function predicts all datasets mentioned in main_data.
			It stores the prediction in the e-term.
		*/
		void predict_data_and_write_to_eterms(DVector<Data*>& main_data, DVector<e_q_term*>& main_cache) {

			assert(main_data.dim == main_cache.dim);
			if (main_data.dim == 0) { return ; }
 

			// do this using only the transpose copy of the training data:
			for (uint ds = 0; ds < main_cache.dim; ds++) {
				e_q_term* m_cache = main_cache(ds);
				Data* m_data = main_data(ds);
				for (uint i = 0; i < m_data->num_cases; i++) {
					m_cache[i].e = 0.0;
					m_cache[i].q = 0.0;
				} 	
			}


			// (1) do the 1/2 sum_f (sum_i v_if x_i)^2 and store it in the e/y-term
			for (int f = 0; f < fm->num_factor; f++) {
				double* v = fm->v.value[f];

				// calculate cache[i].q = sum_i v_if x_i (== q_f-term)
				// Complexity: O(N_z(X^M))
				for (uint ds = 0; ds < main_cache.dim; ds++) {
					e_q_term* m_cache = main_cache(ds);
					Data* m_data = main_data(ds);
					m_data->data_t->begin();
					uint row_index;
					sparse_row<DATA_FLOAT>* feature_data;
					for (uint i = 0; i < m_data->data_t->getNumRows(); i++) {
						{
							row_index = m_data->data_t->getRowIndex();
							feature_data = &(m_data->data_t->getRow()); 
							m_data->data_t->next();
						}
						double& v_if = v[row_index];
					
						for (uint i_fd = 0; i_fd < feature_data->size; i_fd++) {	
							uint& train_case_index = feature_data->data[i_fd].id;		
							FM_FLOAT& x_li = feature_data->data[i_fd].value;	
							m_cache[train_case_index].q += v_if * x_li;			
						}
					}
				}


				// add 0.5*q^2 to e and set q to zero.
				// O(n*|B|)
				for (uint ds = 0; ds < main_cache.dim; ds++) {
					e_q_term* m_cache = main_cache(ds);
					Data* m_data = main_data(ds);
					for (uint c = 0; c < m_data->num_cases; c++) {
						double q_all = m_cache[c].q;
						m_cache[c].e += 0.5 * q_all*q_all;
						m_cache[c].q = 0.0;
					}
				}

			}

			// (2) do -1/2 sum_f (sum_i v_if^2 x_i^2) and store it in the q-term
			for (int f = 0; f < fm->num_factor; f++) {
				double* v = fm->v.value[f];

				// sum up the q^S_f terms in the main-q-cache: 0.5*sum_i (v_if x_i)^2 (== q^S_f-term)
				// Complexity: O(N_z(X^M))
				for (uint ds = 0; ds < main_cache.dim; ds++) {
					e_q_term* m_cache = main_cache(ds);
					Data* m_data = main_data(ds);
		
					m_data->data_t->begin();
					uint row_index;
					sparse_row<DATA_FLOAT>* feature_data;
					for (uint i = 0; i < m_data->data_t->getNumRows(); i++) {
						{
							row_index = m_data->data_t->getRowIndex();
							feature_data = &(m_data->data_t->getRow()); 
							m_data->data_t->next();
						}
						double& v_if = v[row_index];
			
						for (uint i_fd = 0; i_fd < feature_data->size; i_fd++) {	
							uint& train_case_index = feature_data->data[i_fd].id;		
							FM_FLOAT& x_li = feature_data->data[i_fd].value;	
							m_cache[train_case_index].q -= 0.5 * v_if * v_if * x_li * x_li;  
						}
					}
				}

			}	

			// (3) add the w's to the q-term	
			if (fm->k1) {
				for (uint ds = 0; ds < main_cache.dim; ds++) {
					e_q_term* m_cache = main_cache(ds);
					Data* m_data = main_data(ds);
					m_data->data_t->begin();
					uint row_index;
					sparse_row<DATA_FLOAT>* feature_data;
					for (uint i = 0; i < m_data->data_t->getNumRows(); i++) {
						{
							row_index = m_data->data_t->getRowIndex();
							feature_data = &(m_data->data_t->getRow()); 
							m_data->data_t->next();
						}
						double& w_i = fm->w(row_index);						

						for (uint i_fd = 0; i_fd < feature_data->size; i_fd++) {	
							uint& train_case_index = feature_data->data[i_fd].id;		
							FM_FLOAT& x_li = feature_data->data[i_fd].value;	
							m_cache[train_case_index].q += w_i * x_li;
						}
					}
				}
			}	
			// (3) merge both for getting the prediction: w0+e(c)+q(c)
			for (uint ds = 0; ds < main_cache.dim; ds++) {
				e_q_term* m_cache = main_cache(ds);
				Data* m_data = main_data(ds);
			
				for (uint c = 0; c < m_data->num_cases; c++) {
					double q_all = m_cache[c].q;
					m_cache[c].e = m_cache[c].e + q_all;
					if (fm->k0) {
						m_cache[c].e += fm->w0;
					}
					m_cache[c].q = 0.0;
				}
			}

		}
	public:
		
		


	public:
		virtual void predict(Data& data, DVector<double>& out) {
			assert(data.num_cases == out.dim);
			if (do_sample) {
				assert(data.num_cases == pred_sum_all.dim);
				for (uint i = 0; i < out.dim; i++) {
					out(i) = pred_sum_all(i) / num_iter;
				} 
			} else {
				assert(data.num_cases == pred_this.dim);
				for (uint i = 0; i < out.dim; i++) {
					out(i) = pred_this(i);
				} 
			}
			for (uint i = 0; i < out.dim; i++) {
				if (task == TASK_REGRESSION ) {
					out(i) = std::min(max_target, out(i));
					out(i) = std::max(min_target, out(i));
				} else if (task == TASK_CLASSIFICATION) {
					out(i) = std::min(1.0, out(i));
					out(i) = std::max(0.0, out(i));
				} else {
					throw "task not supported";
				}
			}
		}
	protected:



		void add_main_q(Data& train, uint f) {
			// add the q(f)-terms to the main relation q-cache (using only the transpose data)
			
			double* v = fm->v.value[f];


			{
				train.data_t->begin();
				uint row_index;
				sparse_row<DATA_FLOAT>* feature_data;
				for (uint i = 0; i < train.data_t->getNumRows(); i++) {
					{
						row_index = train.data_t->getRowIndex();
						feature_data = &(train.data_t->getRow()); 
						train.data_t->next();
					}
					double& v_if = v[row_index];
					for (uint i_fd = 0; i_fd < feature_data->size; i_fd++) {	
						uint& train_case_index = feature_data->data[i_fd].id;		
						FM_FLOAT& x_li = feature_data->data[i_fd].value;	
						cache[train_case_index].q += v_if * x_li;
					}

				}
			}
		}		

		void draw_all(Data& train) {
			std::ostringstream ss;

			draw_alpha(alpha, train.num_cases);
			if (log != NULL) {
				log->log("alpha", alpha);
			}

			if (fm->k0) {
				draw_w0(fm->w0, fm->reg0, train);
			}
			if (fm->k1) {
				draw_w_lambda(fm->w.value);
				draw_w_mu(fm->w.value);
				if (log != NULL) {
					for (uint g = 0; g < meta->num_attr_groups; g++) {
						ss.str(""); ss << "wmu[" << g << "]"; log->log(ss.str(), w_mu(g));
						ss.str(""); ss << "wlambda[" << g << "]"; log->log(ss.str(), w_lambda(g));
					}
				}

				// draw the w from their posterior
				train.data_t->begin();
				uint row_index;
				sparse_row<DATA_FLOAT>* feature_data;
				for (uint i = 0; i < train.data_t->getNumRows(); i++) {	
					{
						row_index = train.data_t->getRowIndex();
						feature_data = &(train.data_t->getRow()); 
						train.data_t->next();
					}
					uint g = meta->attr_group(row_index);
					draw_w(fm->w(row_index), w_mu(g), w_lambda(g), *feature_data);
				}
				// draw w's for which there is no observation in the training data
				for (uint i = train.data_t->getNumRows(); i < fm->num_attribute; i++) {
					row_index = i;
					feature_data = &(empty_data_row);
					uint g = meta->attr_group(row_index);
					draw_w(fm->w(row_index), w_mu(g), w_lambda(g), *feature_data);
				}

			}

			if (fm->num_factor > 0) {
				draw_v_lambda();
				draw_v_mu();
				if (log != NULL) {
					for (uint g = 0; g < meta->num_attr_groups; g++) {
						for (int f = 0; f < fm->num_factor; f++) {
							ss.str(""); ss << "vmu[" << g << "," << f << "]"; log->log(ss.str(), v_mu(g,f));
							ss.str(""); ss << "vlambda[" << g << "," << f << "]"; log->log(ss.str(), v_lambda(g,f));
						}
					}
				}
			}

			for (int f = 0; f < fm->num_factor; f++) {

				for (uint c = 0; c < train.num_cases; c++) {
					cache[c].q = 0.0;
				}

				add_main_q(train, f);
			
				double* v = fm->v.value[f];
				
				// draw the thetas from their posterior
				train.data_t->begin();
				uint row_index;
				sparse_row<DATA_FLOAT>* feature_data;
				for (uint i = 0; i < train.data_t->getNumRows(); i++) {
					{
						row_index = train.data_t->getRowIndex();
						feature_data = &(train.data_t->getRow()); 
						train.data_t->next();
					}
					uint g = meta->attr_group(row_index);
					draw_v(v[row_index], v_mu(g,f), v_lambda(g,f), *feature_data);
				}		
				// draw v's for which there is no observation in the test data
				for (uint i = train.data_t->getNumRows(); i < fm->num_attribute; i++) {
					row_index = i;
					feature_data = &(empty_data_row);
					uint g = meta->attr_group(row_index);
					draw_v(v[row_index], v_mu(g,f), v_lambda(g,f), *feature_data);
				}
			}		
		}



		// Find the optimal value for the global bias (0-way interaction)
		void draw_w0(double& w0, double& reg, Data& train) {
			// h = 1
			// h^2 = 1
			// \sum e*h = \sum e
			// \sum h^2 = \sum 1
			double w0_sigma_sqr;
			double w0_mean = 0;
			for (uint i = 0; i < train.num_cases; i++) {
				w0_mean += cache[i].e - w0;
			}
			w0_sigma_sqr = (double) 1.0 / (reg + alpha * train.num_cases);
			w0_mean = - w0_sigma_sqr * (alpha * w0_mean - w0_mean_0 * reg);
			// update w0
			double w0_old = w0;

			if (do_sample) {
				w0 = ran_gaussian(w0_mean, std::sqrt(w0_sigma_sqr));
			} else {
				w0 = w0_mean;
			}

			// check for out of bounds values
			if (std::isnan(w0)) {
				nan_cntr_w0++;
				w0 = w0_old;
				assert(! std::isnan(w0_old));
				assert(! std::isnan(w0));
				return;
			}
			if (std::isinf(w0)) {
				inf_cntr_w0++;
				w0 = w0_old;
				assert(! std::isinf(w0_old));
				assert(! std::isinf(w0));
				return;
			}
			// update error
			for (uint i = 0; i < train.num_cases; i++) {
				cache[i].e -= (w0_old - w0);
			}	
		}

		// Find the optimal value for the 1-way interaction w
		void draw_w(double& w, double& w_mu, double& w_lambda, sparse_row<DATA_FLOAT>& feature_data) {
			double w_sigma_sqr = 0;
			double w_mean = 0;
			for (uint i_fd = 0; i_fd < feature_data.size; i_fd++) {	
				uint& train_case_index = feature_data.data[i_fd].id;		
				FM_FLOAT x_li = feature_data.data[i_fd].value;	
				w_mean += x_li * (cache[train_case_index].e - w * x_li);
				w_sigma_sqr += x_li * x_li;
			}
			w_sigma_sqr = (double) 1.0 / (w_lambda + alpha * w_sigma_sqr);
			w_mean = - w_sigma_sqr * (alpha * w_mean - w_mu * w_lambda);

			// update w:
			double w_old = w; 

			if (std::isnan(w_sigma_sqr) || std::isinf(w_sigma_sqr)) { 
				w = 0.0;
			} else {
				if (do_sample) {
					w = ran_gaussian(w_mean, std::sqrt(w_sigma_sqr));
				} else {
					w = w_mean;
				}
			}
			
			// check for out of bounds values
			if (std::isnan(w)) {
				nan_cntr_w++;
				w = w_old;
				assert(! std::isnan(w_old));
				assert(! std::isnan(w));
				return;
			}
			if (std::isinf(w)) {
				inf_cntr_w++;
				w = w_old;
				assert(! std::isinf(w_old));
				assert(! std::isinf(w));
				return;
			}
			// update error:
			for (uint i_fd = 0; i_fd < feature_data.size; i_fd++) {	
				uint& train_case_index = feature_data.data[i_fd].id;	
				FM_FLOAT& x_li = feature_data.data[i_fd].value;	
				double h = x_li;
				cache[train_case_index].e -= h * (w_old - w);	
			}
		}

		// Find the optimal value for the 2-way interaction parameter v
		void draw_v(double& v, double& v_mu, double& v_lambda, sparse_row<DATA_FLOAT>& feature_data) {
			double v_sigma_sqr = 0;
			double v_mean = 0;
			// v_sigma_sqr = \sum h^2 (always)
			// v_mean = \sum h*e (for non_internlock_interactions)
			for (uint i_fd = 0; i_fd < feature_data.size; i_fd++) {	
				uint& train_case_index = feature_data.data[i_fd].id;		
				FM_FLOAT& x_li = feature_data.data[i_fd].value;
				e_q_term* cache_li = &(cache[train_case_index]);
				double h = x_li * ( cache_li->q - x_li * v);
				v_mean += h * cache_li->e;
				v_sigma_sqr += h * h;
			}
			v_mean -= v * v_sigma_sqr;
			v_sigma_sqr = (double) 1.0 / (v_lambda + alpha * v_sigma_sqr);
			v_mean = - v_sigma_sqr * (alpha * v_mean - v_mu * v_lambda);
			
			// update v:
			double v_old = v; 

			if (std::isnan(v_sigma_sqr) || std::isinf(v_sigma_sqr)) { 
				v = 0.0;
			} else {
				if (do_sample) {
					v = ran_gaussian(v_mean, std::sqrt(v_sigma_sqr));
				} else {
					v = v_mean;
				}		
			}
		
			// check for out of bounds values
			if (std::isnan(v)) {
				nan_cntr_v++;
				v = v_old;
				assert(! std::isnan(v_old));
				assert(! std::isnan(v));
				return;
			}
			if (std::isinf(v)) {
				inf_cntr_v++;
				v = v_old;
				assert(! std::isinf(v_old));
				assert(! std::isinf(v));
				return;
			}

			// update error and q:
			for (uint i_fd = 0; i_fd < feature_data.size; i_fd++) {	
				uint& train_case_index = feature_data.data[i_fd].id;		
				FM_FLOAT& x_li = feature_data.data[i_fd].value;	
				e_q_term* cache_li = &(cache[train_case_index]);
				double h = x_li * ( cache_li->q - x_li * v_old);
				cache_li->q -= x_li * (v_old - v);
				cache_li->e -= h * (v_old - v);
			}
		}
	


		void draw_alpha(double& alpha, uint num_train_total) {
			if (! do_multilevel) {
				alpha = alpha_0;
				return;
			}
			double alpha_n = alpha_0 + num_train_total;
			double gamma_n = gamma_0;
			for (uint i = 0; i < num_train_total; i++) {
				gamma_n += cache[i].e*cache[i].e;
			}
			double alpha_old = alpha;
			alpha = ran_gamma(alpha_n / 2.0, gamma_n / 2.0);

			// check for out of bounds values
			if (std::isnan(alpha)) {
				nan_cntr_alpha++;
				alpha = alpha_old;
				assert(! std::isnan(alpha_old));
				assert(! std::isnan(alpha));
				return;
			}
			if (std::isinf(alpha)) {
				inf_cntr_alpha++;
				alpha = alpha_old;
				assert(! std::isinf(alpha_old));
				assert(! std::isinf(alpha));
				return;
			}
		}

		void draw_w_mu(double* w) {
			if (! do_multilevel) {
				w_mu.init(mu_0);
				return;
			}
			DVector<double>& w_mu_mean = cache_for_group_values;
			w_mu_mean.init(0.0);
			for (uint i = 0; i < fm->num_attribute; i++) {
				uint g = meta->attr_group(i);
				w_mu_mean(g) += w[i];
			}
			for (uint g = 0; g < meta->num_attr_groups; g++) {
				w_mu_mean(g) = (w_mu_mean(g)+beta_0 * mu_0) / (meta->num_attr_per_group(g) + beta_0);
				double w_mu_sigma_sqr = (double) 1.0 / ((meta->num_attr_per_group(g) + beta_0) * w_lambda(g));
				double w_mu_old = w_mu(g);
				if (do_sample) {
					w_mu(g) = ran_gaussian(w_mu_mean(g), std::sqrt(w_mu_sigma_sqr));
				} else {
					w_mu(g) = w_mu_mean(g);
				}			

				// check for out of bounds values
				if (std::isnan(w_mu(g))) {
					nan_cntr_w_mu++;
					w_mu(g) = w_mu_old;
					assert(! std::isnan(w_mu_old));
					assert(! std::isnan(w_mu(g)));
					return;
				}
				if (std::isinf(w_mu(g))) {
					inf_cntr_w_mu++;
					w_mu(g) = w_mu_old;
					assert(! std::isinf(w_mu_old));
					assert(! std::isinf(w_mu(g)));
					return;
				}
			}
		}

		void draw_w_lambda(double* w) {
			if (! do_multilevel) {
				return;
			}
				
			DVector<double>& w_lambda_gamma = cache_for_group_values;
			for (uint g = 0; g < meta->num_attr_groups; g++) {
				w_lambda_gamma(g) = beta_0 * (w_mu(g) - mu_0) * (w_mu(g) - mu_0) + gamma_0; 
			}
			for (uint i = 0; i < fm->num_attribute; i++) {
				uint g = meta->attr_group(i);
				w_lambda_gamma(g) += (w[i] - w_mu(g)) * (w[i] - w_mu(g));
			}
			for (uint g = 0; g < meta->num_attr_groups; g++) {
				double w_lambda_alpha = alpha_0 + meta->num_attr_per_group(g) + 1;
				double w_lambda_old = w_lambda(g);
				if (do_sample) {
					w_lambda(g) = ran_gamma(w_lambda_alpha / 2.0, w_lambda_gamma(g) / 2.0);
				} else {
					w_lambda(g) = w_lambda_alpha/w_lambda_gamma(g);
				}
				// check for out of bounds values
				if (std::isnan(w_lambda(g))) {
					nan_cntr_w_lambda++;
					w_lambda(g) = w_lambda_old;
					assert(! std::isnan(w_lambda_old));
					assert(! std::isnan(w_lambda(g)));
					return;
				}
				if (std::isinf(w_lambda(g))) {
					inf_cntr_w_lambda++;
					w_lambda(g) = w_lambda_old;
					assert(! std::isinf(w_lambda_old));
					assert(! std::isinf(w_lambda(g)));
					return;
				}
			}
		}



		void draw_v_mu() {
			if (! do_multilevel) {
				v_mu.init(mu_0);
				return;
			}

			DVector<double>& v_mu_mean = cache_for_group_values;
			for (int f = 0; f < fm->num_factor; f++) {
				v_mu_mean.init(0.0);
				for (uint i = 0; i < fm->num_attribute; i++) {
					uint g = meta->attr_group(i);
					v_mu_mean(g) += fm->v(f,i);
				}
				for (uint g = 0; g < meta->num_attr_groups; g++) {
					v_mu_mean(g) = (v_mu_mean(g) + beta_0 * mu_0) / (meta->num_attr_per_group(g) + beta_0);
					double v_mu_sigma_sqr = (double) 1.0 / ((meta->num_attr_per_group(g) + beta_0) * v_lambda(g,f));
					double v_mu_old = v_mu(g,f);
					if (do_sample) {
						v_mu(g,f) = ran_gaussian(v_mu_mean(g), std::sqrt(v_mu_sigma_sqr));
					} else {
						v_mu(g,f) = v_mu_mean(g);
					}
					if (std::isnan(v_mu(g,f))) {
						nan_cntr_v_mu++;
						v_mu(g,f) = v_mu_old;
						assert(! std::isnan(v_mu_old));
						assert(! std::isnan(v_mu(g,f)));
						return;
					}
					if (std::isinf(v_mu(g,f))) {
						inf_cntr_v_mu++;
						v_mu(g,f) = v_mu_old;
						assert(! std::isinf(v_mu_old));
						assert(! std::isinf(v_mu(g,f)));
						return;
					}
				}
			}
		}

		void draw_v_lambda() {
			if (! do_multilevel) {
				return;
			}

			DVector<double>& v_lambda_gamma = cache_for_group_values;
			for (int f = 0; f < fm->num_factor; f++) {
				for (uint g = 0; g < meta->num_attr_groups; g++) {
					v_lambda_gamma(g) = beta_0 * (v_mu(g,f) - mu_0) * (v_mu(g,f) - mu_0) + gamma_0; 
				}
				for (uint i = 0; i < fm->num_attribute; i++) {
					uint g = meta->attr_group(i);
					v_lambda_gamma(g) += (fm->v(f,i) - v_mu(g,f)) * (fm->v(f,i) - v_mu(g,f));
				}
				for (uint g = 0; g < meta->num_attr_groups; g++) {
					double v_lambda_alpha = alpha_0 + meta->num_attr_per_group(g) + 1;
					double v_lambda_old = v_lambda(g,f);
					if (do_sample) {
						v_lambda(g,f) = ran_gamma(v_lambda_alpha / 2.0, v_lambda_gamma(g) / 2.0);
					} else {
						v_lambda(g,f) = v_lambda_alpha / v_lambda_gamma(g);
					}
					if (std::isnan(v_lambda(g,f))) {
						nan_cntr_v_lambda++;
						v_lambda(g,f) = v_lambda_old;
						assert(! std::isnan(v_lambda_old));
						assert(! std::isnan(v_lambda(g,f)));
						return;
					}
					if (std::isinf(v_lambda(g,f))) {
						inf_cntr_v_lambda++;
						v_lambda(g,f) = v_lambda_old;
						assert(! std::isinf(v_lambda_old));
						assert(! std::isinf(v_lambda(g,f)));
						return;
					}
				}
			}
		}

	public:
		virtual void init() {
			fm_learn::init();

			cache_for_group_values.setSize(meta->num_attr_groups);

			empty_data_row.size = 0;
			empty_data_row.data = NULL;

			alpha_0 = 1.0;
			gamma_0 = 1.0;
			beta_0 = 1.0;
			mu_0 = 0.0;

			alpha = 1;
			
			w0_mean_0 = 0.0;

			w_mu.setSize(meta->num_attr_groups);
			w_lambda.setSize(meta->num_attr_groups);
			w_mu.init(0.0); 
			w_lambda.init(0.0);
		
			v_mu.setSize(meta->num_attr_groups, fm->num_factor);
			v_lambda.setSize(meta->num_attr_groups, fm->num_factor);
			v_mu.init(0.0);
			v_lambda.init(0.0);
		

			if (log != NULL) {
				log->addField("alpha", std::numeric_limits<double>::quiet_NaN());
				if (task == TASK_REGRESSION) {
					log->addField("rmse_mcmc_this", std::numeric_limits<double>::quiet_NaN());
					log->addField("rmse_mcmc_all", std::numeric_limits<double>::quiet_NaN());
					log->addField("rmse_mcmc_all_but5", std::numeric_limits<double>::quiet_NaN());

					//log->addField("rmse_mcmc_test2_this", std::numeric_limits<double>::quiet_NaN());
					//log->addField("rmse_mcmc_test2_all", std::numeric_limits<double>::quiet_NaN());
				} else if (task == TASK_CLASSIFICATION) {
					log->addField("acc_mcmc_this", std::numeric_limits<double>::quiet_NaN());
					log->addField("acc_mcmc_all", std::numeric_limits<double>::quiet_NaN());
					log->addField("acc_mcmc_all_but5", std::numeric_limits<double>::quiet_NaN());
					log->addField("ll_mcmc_this", std::numeric_limits<double>::quiet_NaN());
					log->addField("ll_mcmc_all", std::numeric_limits<double>::quiet_NaN());
					log->addField("ll_mcmc_all_but5", std::numeric_limits<double>::quiet_NaN());
					
					//log->addField("acc_mcmc_test2_this", std::numeric_limits<double>::quiet_NaN());
					//log->addField("acc_mcmc_test2_all", std::numeric_limits<double>::quiet_NaN());
				}

				std::ostringstream ss;
				for (uint g = 0; g < meta->num_attr_groups; g++) {
					ss.str(""); ss << "wmu[" << g << "]"; log->addField(ss.str(), std::numeric_limits<double>::quiet_NaN());
					ss.str(""); ss << "wlambda[" << g << "]"; log->addField(ss.str(), std::numeric_limits<double>::quiet_NaN());
					for (int f = 0; f < fm->num_factor; f++) {
						ss.str(""); ss << "vmu[" << g << "," << f << "]"; log->addField(ss.str(), std::numeric_limits<double>::quiet_NaN());
						ss.str(""); ss << "vlambda[" << g << "," << f << "]"; log->addField(ss.str(), std::numeric_limits<double>::quiet_NaN());
					}
				}
			}
		}
		
		
		virtual void learn(Data& train, Data& test) {
			pred_sum_all.setSize(test.num_cases);
			pred_sum_all_but5.setSize(test.num_cases);
			pred_this.setSize(test.num_cases);
			pred_sum_all.init(0.0);
			pred_sum_all_but5.init(0.0);
			pred_this.init(0.0);

			// init caches data structure
			MemoryLog::getInstance().logNew("e_q_term", sizeof(e_q_term), train.num_cases);
			cache = new e_q_term[train.num_cases];
			MemoryLog::getInstance().logNew("e_q_term", sizeof(e_q_term), test.num_cases);
			cache_test = new e_q_term[test.num_cases];


			_learn(train, test);

			// free data structures
			MemoryLog::getInstance().logFree("e_q_term", sizeof(e_q_term), test.num_cases);
			delete[] cache_test;
			MemoryLog::getInstance().logFree("e_q_term", sizeof(e_q_term), train.num_cases);
			delete[] cache;
		}


		virtual void debug() { 
			fm_learn::debug();
			std::cout << "do_multilevel=" << do_multilevel << std::endl;
			std::cout << "do_sampling=" << do_sample << std::endl;
			std::cout << "num_eval_cases=" << num_eval_cases << std::endl;
		}

};

#endif /*FM_LEARN_MCMC_H_*/