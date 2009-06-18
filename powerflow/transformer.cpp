/** $Id: transformer.cpp 1211 2009-01-17 00:45:29Z d3x593 $
	Copyright (C) 2008 Battelle Memorial Institute
	@file transformer.cpp
	@addtogroup powerflow_transformer Transformer
	@ingroup powerflow

	The transformer is one of the more complex objects in the powerflow network.
	Implemented as a link, the transformer configuration exports a type property
	that allows the transformer to operate as and single phase transformer, a
	wye-wye connected transformer, a delta-grounded wye tranformer, a delta-delta
	transformer, and as a center-tapped transformer.
	
	The transformer exports a phase property that is a set of phases and may be 
	set using the bitwise or operator (A|B|C for a 3 phase line).
 @{
**/

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <math.h>
#include <iostream>
using namespace std;

#include "transformer.h"

CLASS* transformer::oclass = NULL;
CLASS* transformer::pclass = NULL;

transformer::transformer(MODULE *mod) : link(mod)
{
	if(oclass == NULL)
	{
		pclass = link::oclass;
		
		oclass = gl_register_class(mod,"transformer",sizeof(transformer),PC_PRETOPDOWN|PC_BOTTOMUP|PC_POSTTOPDOWN|PC_UNSAFE_OVERRIDE_OMIT);
        if(oclass == NULL)
            GL_THROW("unable to register object class implemented by %s",__FILE__);
        
        if(gl_publish_variable(oclass,
			PT_INHERIT, "link",
           	PT_object, "configuration", PADDR(configuration),
			NULL) < 1) GL_THROW("unable to publish properties in %s",__FILE__);
    }
}

int transformer::isa(char *classname)
{
	return strcmp(classname,"transformer")==0 || link::isa(classname);
}

int transformer::create()
{
	int result = link::create();
	configuration = NULL;
	return result;
}

int transformer::init(OBJECT *parent)
{
	if (!configuration)
		throw "no transformer configuration specified.";
		/*  TROUBLESHOOT
		A transformer configuration was not provided.  Please use object transformer_configuration
		and define the necessary parameters of your transformer to continue.
		*/
	if (!gl_object_isa(configuration, "transformer_configuration"))
		throw "invalid transformer configuration";
		/*  TROUBLESHOOT
		An invalid transformer configuration was provided.  Ensure you have proper values in each field
		of the transformer_configuration object and that you haven't inadvertantly used a line configuration
		as the transformer configuration.
		*/

	double V_base,za_basehi,za_baselo,V_basehi;
	double sa_base;
	double nt, nt_a, nt_b, nt_c, inv_nt_a, inv_nt_b, inv_nt_c;
	complex zt, zt_a, zt_b, zt_c, z0, z1, z2, zc;

	transformer_configuration *config = OBJECTDATA(configuration,
	                                   transformer_configuration);

	if (config->connect_type==3)		//Flag Delta-Gwye and Split-phase for phase checks
		SpecialLnk = DELTAGWYE;
	else if (config->connect_type==5)
		SpecialLnk = SPLITPHASE;

	link::init(parent);
	OBJECT *obj = OBJECTHDR(this);

	V_base = config->V_secondary;
	voltage_ratio = nt = config->V_primary / config->V_secondary;
	zt = (config->impedance * V_base * V_base) / (config->kVA_rating * 1000.0);

	for (int i = 0; i < 3; i++) 
	{
		for (int j = 0; j < 3; j++) 
			a_mat[i][j] = b_mat[i][j] = c_mat[i][j] = d_mat[i][j] = A_mat[i][j] = B_mat[i][j] = 0.0;
	}

	switch (config->connect_type) {
		case transformer_configuration::WYE_WYE: 
		case transformer_configuration::SINGLE_PHASE:
			if (has_phase(PHASE_A)) 
			{
				nt_a = nt;
				zt_a = zt * nt_a;
				inv_nt_a = 1 / nt_a;
			} 
			else 
			{
				nt_a = inv_nt_a = 0.0;
				zt_a = complex(0,0);
			}

			if (has_phase(PHASE_B)) 
			{
				nt_b = nt;
				zt_b = zt * nt_b;
				inv_nt_b = 1 / nt_b;
			} 
			else 
			{
				nt_b = inv_nt_b = 0.0;
				zt_b = complex(0,0);
			}

			if (has_phase(PHASE_C)) 
			{
				nt_c = nt;
				zt_c = zt * nt_c;
				inv_nt_c = 1 / nt_c;
			} 
			else 
			{
				nt_c = inv_nt_c = 0.0;
				zt_c = complex(0,0);
			}
			
			if (solver_method==SM_FBS)
			{
				b_mat[0][0] = zt_a;
				b_mat[1][1] = zt_b;
				b_mat[2][2] = zt_c;
			}
			else if (solver_method==SM_GS)
			{
				complex Izt = complex(1,0) / zt;

				b_mat[0][0] = b_mat[1][1] = b_mat[2][2] = Izt;
			}
			else if (solver_method==SM_NR)
			{
				throw "Newton-Raphson solution method is not yet supported";
			}
			else 
			{
				GL_THROW("Unsupported solver method");
				/*  TROUBLESHOOT
				An unsupported solver type was detected.  Valid solver types are FBS
				(forward-back sweep), GS (Gauss-Seidel), and NR (Newton-Raphson).  Please use
				one of these methods and consider submitting a bug report for the solver type you tried.
				*/
			}

			a_mat[0][0] = nt_a;
			a_mat[1][1] = nt_b;
			a_mat[2][2] = nt_c;

			B_mat[0][0] = zt;
			B_mat[1][1] = zt;
			B_mat[2][2] = zt;

			d_mat[0][0] = A_mat[0][0] = inv_nt_a;
			d_mat[1][1] = A_mat[1][1] = inv_nt_b;
			d_mat[2][2] = A_mat[2][2] = inv_nt_c;

			break;
		case transformer_configuration::DELTA_DELTA:
			if (solver_method==SM_FBS)
			{
				a_mat[0][0] = a_mat[1][1] = a_mat[2][2] = nt * 2.0 / 3.0;
				a_mat[0][1] = a_mat[0][2] = a_mat[1][0] = a_mat[1][2] = a_mat[2][0] = a_mat[2][1] = -nt / 3.0;

				b_mat[0][0] = b_mat[1][1] = zt * nt;
				b_mat[2][0] = b_mat[2][1] = zt * -nt;

				d_mat[0][0] = d_mat[1][1] = d_mat[2][2] = complex(1.0) / nt;

				A_mat[0][0] = A_mat[1][1] = A_mat[2][2] = complex(2.0) / (nt * 3.0);
				A_mat[0][1] = A_mat[0][2] = A_mat[1][0] = A_mat[1][2] = A_mat[2][0] = A_mat[2][1] = complex(-1.0) / (nt * 3.0);

				B_mat[0][0] = B_mat[1][1] = zt;
				B_mat[2][0] = B_mat[2][1] = -zt;
			}
			else if (solver_method==SM_GS)
			{
				//Calculate admittance matrix
				complex Izt = complex(1,0) / zt;

				b_mat[0][0] = b_mat[1][1] = b_mat[2][2] = Izt;

				a_mat[0][0] = a_mat[1][1] = a_mat[2][2] = nt * 2.0 / 3.0;
				a_mat[0][1] = a_mat[0][2] = a_mat[1][0] = a_mat[1][2] = a_mat[2][0] = a_mat[2][1] = -nt / 3.0;

				d_mat[0][0] = d_mat[1][1] = d_mat[2][2] = complex(1.0) / nt;

				A_mat[0][0] = A_mat[1][1] = A_mat[2][2] = complex(2.0) / (nt * 3.0);
				A_mat[0][1] = A_mat[0][2] = A_mat[1][0] = A_mat[1][2] = A_mat[2][0] = A_mat[2][1] = complex(-1.0) / (nt * 3.0);

				B_mat[0][0] = B_mat[1][1] = zt;
				B_mat[2][0] = B_mat[2][1] = -zt;
			}
			else if (solver_method==SM_NR)
			{
				throw "Newton-Raphson solution method is not yet supported";
			}
			else 
			{
				GL_THROW("Unsupported solver method");
				// Defined above
			}
			break;
		case transformer_configuration::DELTA_GWYE:
			if (solver_method==SM_FBS)
			{
				if (nt>1.0)//step down transformer
				{
					nt *= sqrt(3.0);
					
					a_mat[0][1] = a_mat[1][2] = a_mat[2][0] = -nt * 2.0 / 3.0;
					a_mat[0][2] = a_mat[1][0] = a_mat[2][1] = -nt / 3.0;

					b_mat[0][1] = b_mat[1][2] = b_mat[2][0] = zt * -nt * 2.0 / 3.0;    
					b_mat[0][2] = b_mat[1][0] = b_mat[2][1] = zt * -nt / 3.0;    
					
					d_mat[0][0] = d_mat[1][1] = d_mat[2][2] = complex(1.0) / nt;
					d_mat[0][1] = d_mat[1][2] = d_mat[2][0] = complex(-1.0) / nt;

					A_mat[0][0] = A_mat[1][1] = A_mat[2][2] = complex(1.0) / nt;
					A_mat[0][2] = A_mat[1][0] = A_mat[2][1] = complex(-1.0) / nt;

					B_mat[0][0] = B_mat[1][1] = B_mat[2][2] = zt;
				}
				else {//step up transformer
					nt *= sqrt(3.0);
					
					a_mat[0][0] = a_mat[1][1] = a_mat[2][2] = nt * 2.0 / 3.0;
					a_mat[0][1] = a_mat[1][2] = a_mat[2][0] = nt / 3.0;

					b_mat[0][0] = b_mat[1][1] = b_mat[2][2] = zt * nt * 2.0 / 3.0;    
					b_mat[0][1] = b_mat[1][2] = b_mat[2][0] = zt * nt/ 3.0;    
					
					d_mat[0][0] = d_mat[1][1] = d_mat[2][2] = complex(1.0) / nt;
					d_mat[0][2] = d_mat[1][0] = d_mat[2][1] = complex(-1.0) / nt;

					A_mat[0][0] = A_mat[1][1] = A_mat[2][2] = complex(1.0) / nt;
					A_mat[0][1] = A_mat[1][2] = A_mat[2][0] = complex(-1.0) / nt;

					B_mat[0][0] = B_mat[1][1] = B_mat[2][2] = zt;
				}
			}
			else if (solver_method==SM_GS)
			{
				SpecialLnk = DELTAGWYE;

				complex Izt = complex(1.0,0) / zt;

				complex alphaval = voltage_ratio * sqrt(3.0);

				nt *=sqrt(3.0);	//Adjustment for other matrices

				if (voltage_ratio>1.0) //Step down
				{
					//High->low voltage change
					c_mat[0][0] = c_mat[1][1] = c_mat[2][2] = complex(1.0) / alphaval;
					c_mat[0][2] = c_mat[1][0] = c_mat[2][1] = complex(-1.0) / alphaval;
					c_mat[0][1] = c_mat[1][2] = c_mat[2][0] = 0.0;

					//Y-based impedance model
					b_mat[0][0] = b_mat[1][1] = b_mat[2][2] = Izt;
					b_mat[0][1] = b_mat[0][2] = b_mat[1][0] = 0.0;
					b_mat[1][2] = b_mat[2][0] = b_mat[2][1] = 0.0;

					//I-low to I-high change
					B_mat[0][0] = B_mat[1][1] = B_mat[2][2] = complex(1.0) / alphaval;
					B_mat[0][1] = B_mat[1][2] = B_mat[2][0] = complex(-1.0) / alphaval;
					B_mat[0][2] = B_mat[1][0] = B_mat[2][1] = 0.0;

					//Other matrices (stolen from above)
					a_mat[0][1] = a_mat[1][2] = a_mat[2][0] = -nt * 2.0 / 3.0;
					a_mat[0][2] = a_mat[1][0] = a_mat[2][1] = -nt / 3.0;

					d_mat[0][0] = d_mat[1][1] = d_mat[2][2] = complex(1.0) / nt;
					d_mat[0][1] = d_mat[1][2] = d_mat[2][0] = complex(-1.0) / nt;

					A_mat[0][0] = A_mat[1][1] = A_mat[2][2] = complex(1.0) / nt;
					A_mat[0][2] = A_mat[1][0] = A_mat[2][1] = complex(-1.0) / nt;
				}
				else //assume step up
				{
					//Low->high voltage change
					c_mat[0][0] = c_mat[1][1] = c_mat[2][2] = complex(1.0) / alphaval;
					c_mat[0][1] = c_mat[1][2] = c_mat[2][0] = complex(-1.0) / alphaval;
					c_mat[0][2] = c_mat[1][0] = c_mat[2][1] = 0.0;

					//Impedance matrix
					b_mat[0][0] = b_mat[1][1] = b_mat[2][2] = Izt;
					b_mat[0][1] = b_mat[0][2] = b_mat[1][0] = 0.0;
					b_mat[1][2] = b_mat[2][0] = b_mat[2][1] = 0.0;

					//I-high to I-low change
					B_mat[0][0] = B_mat[1][1] = B_mat[2][2] = complex(1.0) / alphaval;
					B_mat[0][2] = B_mat[1][0] = B_mat[2][1] = complex(-1.0) / alphaval;
					B_mat[0][1] = B_mat[1][2] = B_mat[2][0] = 0.0;

					//Other matrices (stolen from above)
					a_mat[0][0] = a_mat[1][1] = a_mat[2][2] = nt * 2.0 / 3.0;
					a_mat[0][1] = a_mat[1][2] = a_mat[2][0] = nt / 3.0;

					d_mat[0][0] = d_mat[1][1] = d_mat[2][2] = complex(1.0) / nt;
					d_mat[0][2] = d_mat[1][0] = d_mat[2][1] = complex(-1.0) / nt;

					A_mat[0][0] = A_mat[1][1] = A_mat[2][2] = complex(1.0) / nt;
					A_mat[0][1] = A_mat[1][2] = A_mat[2][0] = complex(-1.0) / nt;
				}
			}
			else if (solver_method==SM_NR)
			{
				throw "Newton-Raphson solution method is not yet supported";
			}
			else 
			{
				GL_THROW("Unsupported solver method");
				// Defined above
			}
		
			break;
		case transformer_configuration::SINGLE_PHASE_CENTER_TAPPED:
			if (solver_method==SM_FBS)
			{
				if (has_phase(PHASE_A|PHASE_B)) // delta AB
				{
					throw "delta split tap is not supported yet";
				}
				else if (has_phase(PHASE_B|PHASE_C)) // delta AB
				{
					throw "delta split tap is not supported yet";
				}
				else if (has_phase(PHASE_A|PHASE_C)) // delta AB
				{
					throw "delta split tap is not supported yet";
				}
				else if (has_phase(PHASE_A)) // wye-A
				{
					V_basehi = config->V_primary;
					sa_base = config->phaseA_kVA_rating;
					if (sa_base==0)	//Error
						GL_THROW("Split-phase tranformer:%d trying to attach to phase A not defined in the configuration",obj->id);
						/*  TROUBLESHOOT
						A single-phase, center-tapped transformer is attempting to attach to a system with phase A, while
						its phase A is undefined.  Fix the appropriate link or define a new transformer configuration with
						powerA_rating property properly defined.
						*/

					za_basehi = (V_basehi*V_basehi)/(sa_base*1000);
					za_baselo = (V_base * V_base)/(sa_base*1000);
					z0 = complex(0.5 * config->impedance.Re(),0.8*config->impedance.Im()) * complex(za_basehi,0);
					z1 = complex(config->impedance.Re(),0.4 * config->impedance.Im()) * complex(za_baselo,0);
					z2 = complex(config->impedance.Re(),0.4 * config->impedance.Im()) * complex(za_baselo,0);
					zc =  complex(za_basehi,0) * complex(1000000 * config->impedance.Re(),0) * complex(0,1000 * config->impedance.Im()) / complex(1000000 * config->impedance.Re(),1000 * config->impedance.Im());
					zt_b = complex(0,0);
					zt_c = complex(0,0);
					
					a_mat[0][0] = a_mat[1][0] = (z0 / zc + complex(1,0))*nt;
					
					c_mat[0][0] = complex(1,0)*nt / zc;
				
					d_mat[0][0] = complex(1,0)/nt + complex(nt,0)*z1 / zc;
					d_mat[0][1] = complex(-1,0)/nt;

					A_mat[0][0] = A_mat[1][0] =  (zc / (zc + z0) ) * complex(1,0)/nt;
				}

				else if (has_phase(PHASE_B)) // wye-B
				{
					V_basehi = config->V_primary;
					sa_base = config->phaseB_kVA_rating;
					if (sa_base==0)	//Error
						GL_THROW("Split-phase tranformer:%d trying to attach to phase B not defined in the configuration",obj->id);
						/*  TROUBLESHOOT
						A single-phase, center-tapped transformer is attempting to attach to a system with phase B, while
						its phase B is undefined.  Fix the appropriate link or define a new transformer configuration with
						powerB_rating property properly defined.
						*/

					za_basehi = (V_basehi*V_basehi)/(sa_base*1000);
					za_baselo = (V_base * V_base)/(sa_base*1000);
					z0 = complex(0.5 * config->impedance.Re(),0.8*config->impedance.Im()) * complex(za_basehi,0);
					z1 = complex(config->impedance.Re(),0.4 * config->impedance.Im()) * complex(za_baselo,0);
					z2 = complex(config->impedance.Re(),0.4 * config->impedance.Im()) * complex(za_baselo,0);
					zc =  complex(za_basehi,0) * complex(1000000 * config->impedance.Re(),0) * complex(0,1000 * config->impedance.Im()) / complex(1000000 * config->impedance.Re(),1000 * config->impedance.Im());
					zt_b = complex(0,0);
					zt_c = complex(0,0);
					
					a_mat[0][1] = a_mat[1][1] = (z0 / zc + complex(1,0))*nt;
				
					c_mat[1][0] = complex(1,0)*nt / zc;

					d_mat[1][0] = complex(1,0)/nt + complex(nt,0)*z1 / zc;
					d_mat[1][1] = complex(-1,0)/nt;

					A_mat[0][1] = A_mat[1][1] = (zc / (zc + z0) ) * complex(1,0)/nt;			
				}
				else if (has_phase(PHASE_C)) // wye-C
				{
					V_basehi = config->V_primary;
					sa_base = config->phaseC_kVA_rating;
					if (sa_base==0)	//Error
						GL_THROW("Split-phase tranformer:%d trying to attach to phase C not defined in the configuration",obj->id);
						/*  TROUBLESHOOT
						A single-phase, center-tapped transformer is attempting to attach to a system with phase C, while
						its phase C is undefined.  Fix the appropriate link or define a new transformer configuration with
						powerC_rating property properly defined.
						*/

					za_basehi = (V_basehi*V_basehi)/(sa_base*1000);
					za_baselo = (V_base * V_base)/(sa_base*1000);
					z0 = complex(0.5 * config->impedance.Re(),0.8*config->impedance.Im()) * complex(za_basehi,0);
					z1 = complex(config->impedance.Re(),0.4 * config->impedance.Im()) * complex(za_baselo,0);
					z2 = complex(config->impedance.Re(),0.4 * config->impedance.Im()) * complex(za_baselo,0);
					zc =  complex(za_basehi,0) * complex(1000000 * config->impedance.Re(),0) * complex(0,1000 * config->impedance.Im()) / complex(1000000 * config->impedance.Re(),1000 * config->impedance.Im());
					zt_b = complex(0,0);
					zt_c = complex(0,0);
					
					a_mat[0][2] = a_mat[1][2] = (z0 / zc + complex(1,0))*nt;
				
					c_mat[2][0] = complex(1,0)*nt / zc;

					d_mat[2][0] = complex(1,0)/nt + complex(nt,0)*z1 / zc;
					d_mat[2][1] = complex(-1,0)/nt;

					A_mat[0][2] = A_mat[1][2] = (zc / (zc + z0) ) * complex(1,0)/nt; 
				}

				b_mat[0][0] = (z0 / zc + complex(1,0))*(z1*nt) + z0/nt;
				b_mat[0][1] = complex(-1,0) * (z0/nt);
				b_mat[0][2] = complex(0,0);
				b_mat[1][0] = (z0/nt);
				b_mat[1][1] = -(z0 / zc + complex(1,0))*(z2*nt) - z0/nt;
				b_mat[1][2] = complex(0,0);
				b_mat[2][0] = complex(0,0);
				b_mat[2][1] = complex(0,0);
				b_mat[2][2] = complex(0,0);

				B_mat[0][0] = (z1) + (z0*zc/((zc + z0)*nt*nt));
				B_mat[0][1] = -(z0*zc/((zc + z0)*nt*nt));
				B_mat[1][0] = (z0*zc/((zc + z0)*nt*nt));
				B_mat[1][1] = complex(-1,0) * ((z2) + (z0*zc/((zc + z0)*nt*nt)));
				B_mat[1][2] = complex(0,0);
				B_mat[2][0] = complex(0,0);
				B_mat[2][1] = complex(0,0);
				B_mat[2][2] = complex(0,0);
			}
			else if (solver_method==SM_GS)	// This doesn't work yet
			{
				GL_THROW("Gauss-Seidel Implementation of Split-Phase is not complete");
				/*  TROUBLESHOOT
				At this time, the Gauss-Seidel method does not support split-phase transformers.
				This will hopefully be a feature in future releases.
				*/
			}
			else if (solver_method==SM_NR)
			{
				throw "Newton-Raphson solution method is not yet supported";
			}
			else 
			{
				throw "Unsupported solver method";
				//defined above
			}

			break;
		default:
			throw "unknown transformer connect type";
			/*  TROUBLESHOOT
			An unknown transformer configuration was provided.  Please ensure you are using
			only the defined types of transformer.  Refer to the documentation of use the command flag
			--modhelp powerflow:transformer_configuration to see valid transformer types.
			*/
	}

#ifdef _TESTING
	extern bool show_matrix_values;
	if (show_matrix_values)
	{
		gl_testmsg("transformer:\ta matrix");
		print_matrix(a_mat);
		gl_testmsg("transformer:\tA matrix");
		print_matrix(A_mat);
		gl_testmsg("transformer:\tb matrix");
		print_matrix(b_mat);
		gl_testmsg("transformer:\tB matrix");
		print_matrix(B_mat);
		gl_testmsg("transformer:\td matrix");
		print_matrix(d_mat);
	}
#endif

	return 1;
}

//////////////////////////////////////////////////////////////////////////
// IMPLEMENTATION OF CORE LINKAGE: transformer
//////////////////////////////////////////////////////////////////////////

/**
* REQUIRED: allocate and initialize an object.
*
* @param obj a pointer to a pointer of the last object in the list
* @param parent a pointer to the parent of this object
* @return 1 for a successfully created object, 0 for error
*/
EXPORT int create_transformer(OBJECT **obj, OBJECT *parent)
{
	try
	{
		*obj = gl_create_object(transformer::oclass);
		if (*obj!=NULL)
		{
			transformer *my = OBJECTDATA(*obj,transformer);
			gl_set_parent(*obj,parent);
			return my->create();
		}
	}
	catch (char *msg)
	{
		gl_error("create_transformer: %s", msg);
	}
	return 0;
}



/**
* Object initialization is called once after all object have been created
*
* @param obj a pointer to this object
* @return 1 on success, 0 on error
*/
EXPORT int init_transformer(OBJECT *obj)
{
	transformer *my = OBJECTDATA(obj,transformer);
	try {
		return my->init(obj->parent);
	}
	catch (char *msg)
	{
		GL_THROW("%s (transformer:%d): %s", my->get_name(), my->get_id(), msg);
		return 0; 
	}
}

/**
* Sync is called when the clock needs to advance on the bottom-up pass (PC_BOTTOMUP)
*
* @param obj the object we are sync'ing
* @param t0 this objects current timestamp
* @param pass the current pass for this sync call
* @return t1, where t1>t0 on success, t1=t0 for retry, t1<t0 on failure
*/
EXPORT TIMESTAMP sync_transformer(OBJECT *obj, TIMESTAMP t0, PASSCONFIG pass)
{
	transformer *pObj = OBJECTDATA(obj,transformer);
	try {
		TIMESTAMP t1 = TS_NEVER;
		switch (pass) {
		case PC_PRETOPDOWN:
			return pObj->presync(t0);
		case PC_BOTTOMUP:
			return pObj->sync(t0);
		case PC_POSTTOPDOWN:
			t1 = pObj->postsync(t0);
			obj->clock = t0;
			return t1;
		default:
			throw "invalid pass request";
		}
	} catch (const char *error) {
		GL_THROW("%s (transformer:%d): %s", pObj->get_name(), pObj->get_id(), error);
		return 0; 
	} catch (...) {
		GL_THROW("%s (transformer:%d): %s", pObj->get_name(), pObj->get_id(), "unknown exception");
		return 0;
	}
}

EXPORT int isa_transformer(OBJECT *obj, char *classname)
{
	return OBJECTDATA(obj,transformer)->isa(classname);
}

/**@}**/
