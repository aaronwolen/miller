#include <stdlib.h>
#include "lib/mlr_globals.h"
#include "lib/mlrutil.h"
#include "mlr_dsl_cst.h"
#include "context_flags.h"

// ================================================================
typedef struct _srec_assignment_state_t {
	char*             srec_lhs_field_name;
	rval_evaluator_t* prhs_evaluator;
} srec_assignment_state_t;

static mlr_dsl_cst_statement_handler_t handle_srec_assignment;
static mlr_dsl_cst_statement_freer_t free_srec_assignment;

// ----------------------------------------------------------------
mlr_dsl_cst_statement_t* alloc_srec_assignment(mlr_dsl_cst_t* pcst, mlr_dsl_ast_node_t* pnode,
	int type_inferencing, int context_flags)
{
	srec_assignment_state_t* pstate = mlr_malloc_or_die(sizeof(srec_assignment_state_t));

	pstate->prhs_evaluator = NULL;

	MLR_INTERNAL_CODING_ERROR_IF((pnode->pchildren == NULL) || (pnode->pchildren->length != 2));

	mlr_dsl_ast_node_t* pleft  = pnode->pchildren->phead->pvvalue;
	mlr_dsl_ast_node_t* pright = pnode->pchildren->phead->pnext->pvvalue;

	MLR_INTERNAL_CODING_ERROR_IF(pleft->type != MD_AST_NODE_TYPE_FIELD_NAME);
	MLR_INTERNAL_CODING_ERROR_IF(pleft->pchildren != NULL);

	pstate->srec_lhs_field_name = pleft->text;
	pstate->prhs_evaluator = rval_evaluator_alloc_from_ast(pright, pcst->pfmgr, type_inferencing, context_flags);

	return mlr_dsl_cst_statement_valloc(
		pnode,
		handle_srec_assignment,
		free_srec_assignment,
		pstate);
}

// ----------------------------------------------------------------
static void free_srec_assignment(mlr_dsl_cst_statement_t* pstatement) {
	srec_assignment_state_t* pstate = pstatement->pvstate;

	pstate->prhs_evaluator->pfree_func(pstate->prhs_evaluator);

	free(pstate);
}

// ----------------------------------------------------------------
static void handle_srec_assignment(
	mlr_dsl_cst_statement_t* pstatement,
	variables_t*             pvars,
	cst_outputs_t*           pcst_outputs)
{
	srec_assignment_state_t* pstate = pstatement->pvstate;

	char* srec_lhs_field_name = pstate->srec_lhs_field_name;

	rval_evaluator_t* prhs_evaluator = pstate->prhs_evaluator;
	mv_t val = prhs_evaluator->pprocess_func(prhs_evaluator->pvstate, pvars);

	// Write typed mlrval output to the typed overlay rather than into the lrec (which holds only
	// string values).
	//
	// The rval_evaluator reads the overlay in preference to the lrec. E.g. if the input had
	// "x"=>"abc","y"=>"def" but the previous pass through this loop set "y"=>7.4 and "z"=>"ghi" then an
	// expression right-hand side referring to $y would get the floating-point value 7.4. So we don't need
	// to do lrec_put here, and moreover should not for two reasons: (1) there is a performance hit of doing
	// throwaway number-to-string formatting -- it's better to do it once at the end; (2) having the string
	// values doubly owned by the typed overlay and the lrec would result in double frees, or awkward
	// bookkeeping. However, the NR variable evaluator reads prec->field_count, so we need to put something
	// here. And putting something statically allocated minimizes copying/freeing.
	if (mv_is_present(&val)) {
		lhmsmv_put(pvars->ptyped_overlay, srec_lhs_field_name, &val, FREE_ENTRY_VALUE);
		lrec_put(pvars->pinrec, srec_lhs_field_name, "bug", NO_FREE);
	} else {
		mv_free(&val);
	}
}

// ================================================================
typedef struct _indirect_srec_assignment_state_t {
	rval_evaluator_t* plhs_evaluator;
	rval_evaluator_t* prhs_evaluator;
} indirect_srec_assignment_state_t;

static mlr_dsl_cst_statement_handler_t handle_indirect_srec_assignment;
static mlr_dsl_cst_statement_freer_t free_indirect_srec_assignment;

// ----------------------------------------------------------------
// $ mlr --from ../data/small put -v '$[@x] = 1'
// AST ROOT:
// text="block", type=STATEMENT_BLOCK:
//     text="=", type=INDIRECT_SREC_ASSIGNMENT:
//         text="oosvar_keylist", type=OOSVAR_KEYLIST:
//             text="x", type=STRING_LITERAL.
//         text="1", type=NUMERIC_LITERAL.

// ----------------------------------------------------------------
mlr_dsl_cst_statement_t* alloc_indirect_srec_assignment(mlr_dsl_cst_t* pcst, mlr_dsl_ast_node_t* pnode,
	int type_inferencing, int context_flags)
{
	indirect_srec_assignment_state_t* pstate = mlr_malloc_or_die(sizeof(indirect_srec_assignment_state_t));

	pstate->prhs_evaluator = NULL;

	MLR_INTERNAL_CODING_ERROR_IF((pnode->pchildren == NULL) || (pnode->pchildren->length != 2));

	mlr_dsl_ast_node_t* pleft  = pnode->pchildren->phead->pvvalue;
	mlr_dsl_ast_node_t* pright = pnode->pchildren->phead->pnext->pvvalue;

	pstate->plhs_evaluator = rval_evaluator_alloc_from_ast(pleft,  pcst->pfmgr, type_inferencing, context_flags);
	pstate->prhs_evaluator = rval_evaluator_alloc_from_ast(pright, pcst->pfmgr, type_inferencing, context_flags);

	return mlr_dsl_cst_statement_valloc(
		pnode,
		handle_indirect_srec_assignment,
		free_indirect_srec_assignment,
		pstate);
}

// ----------------------------------------------------------------
static void free_indirect_srec_assignment(mlr_dsl_cst_statement_t* pstatement) {
	indirect_srec_assignment_state_t* pstate = pstatement->pvstate;

	pstate->plhs_evaluator->pfree_func(pstate->plhs_evaluator);
	pstate->prhs_evaluator->pfree_func(pstate->prhs_evaluator);

	free(pstate);
}

// ----------------------------------------------------------------
static void handle_indirect_srec_assignment(
	mlr_dsl_cst_statement_t* pstatement,
	variables_t*             pvars,
	cst_outputs_t*           pcst_outputs)
{
	indirect_srec_assignment_state_t* pstate = pstatement->pvstate;

	rval_evaluator_t* plhs_evaluator = pstate->plhs_evaluator;
	rval_evaluator_t* prhs_evaluator = pstate->prhs_evaluator;

	mv_t lval = plhs_evaluator->pprocess_func(plhs_evaluator->pvstate, pvars);
	mv_t rval = prhs_evaluator->pprocess_func(prhs_evaluator->pvstate, pvars);

	char free_flags;
	char* srec_lhs_field_name = mv_format_val(&lval, &free_flags);

	// Write typed mlrval output to the typed overlay rather than into the lrec (which holds only
	// string values).
	//
	// The rval_evaluator reads the overlay in preference to the lrec. E.g. if the input had
	// "x"=>"abc","y"=>"def" but the previous pass through this loop set "y"=>7.4 and "z"=>"ghi" then an
	// expression right-hand side referring to $y would get the floating-point value 7.4. So we don't need
	// to do lrec_put here, and moreover should not for two reasons: (1) there is a performance hit of doing
	// throwaway number-to-string formatting -- it's better to do it once at the end; (2) having the string
	// values doubly owned by the typed overlay and the lrec would result in double frees, or awkward
	// bookkeeping. However, the NR variable evaluator reads prec->field_count, so we need to put something
	// here. And putting something statically allocated minimizes copying/freeing.
	if (mv_is_present(&rval)) {
		lhmsmv_put(pvars->ptyped_overlay, mlr_strdup_or_die(srec_lhs_field_name), &rval,
			FREE_ENTRY_KEY|FREE_ENTRY_VALUE);
		lrec_put(pvars->pinrec, mlr_strdup_or_die(srec_lhs_field_name), "bug", FREE_ENTRY_KEY | FREE_ENTRY_KEY);
	} else {
		mv_free(&rval);
	}

	if (free_flags) {
		free(srec_lhs_field_name);
	}
}

// ================================================================
typedef struct _local_variable_definition_state_t {
	char*             lhs_variable_name;
	int               lhs_frame_relative_index;
	int               lhs_type_mask;
	rval_evaluator_t* prhs_evaluator;
} local_variable_definition_state_t;

static mlr_dsl_cst_statement_handler_t handle_local_variable_definition;
static mlr_dsl_cst_statement_handler_t handle_local_variable_definition_from_map_literal;
static mlr_dsl_cst_statement_freer_t free_local_variable_definition;

// ----------------------------------------------------------------
mlr_dsl_cst_statement_t* alloc_local_variable_definition( // xxx XXX mapvars next
	mlr_dsl_cst_t*      pcst,
	mlr_dsl_ast_node_t* pnode,
	int                 type_inferencing,
	int                 context_flags,
	int                 type_mask)
{
	local_variable_definition_state_t* pstate = mlr_malloc_or_die(
		sizeof(local_variable_definition_state_t));

	mlr_dsl_ast_node_t* pname_node = pnode->pchildren->phead->pvvalue;
	pstate->lhs_variable_name = pname_node->text;
	MLR_INTERNAL_CODING_ERROR_IF(pname_node->vardef_frame_relative_index == MD_UNUSED_INDEX);
	pstate->lhs_frame_relative_index = pname_node->vardef_frame_relative_index;
	pstate->lhs_type_mask = type_mask;

	mlr_dsl_cst_statement_handler_t* pstatement_handler = NULL;
	mlr_dsl_ast_node_t* prhs_node = pnode->pchildren->phead->pnext->pvvalue;
	if (prhs_node->type == MD_AST_NODE_TYPE_MAP_LITERAL) {
		pstate->prhs_evaluator = NULL; // xxx more to do for mapvars
		pstatement_handler = handle_local_variable_definition_from_map_literal;
	} else {
		pstate->prhs_evaluator = rval_evaluator_alloc_from_ast(prhs_node, pcst->pfmgr, type_inferencing, context_flags);
		pstatement_handler = handle_local_variable_definition;
	}

	return mlr_dsl_cst_statement_valloc(
		pnode,
		pstatement_handler,
		free_local_variable_definition,
		pstate);
}

// ----------------------------------------------------------------
static void free_local_variable_definition(mlr_dsl_cst_statement_t* pstatement) {
	local_variable_definition_state_t* pstate = pstatement->pvstate;

	if (pstate->prhs_evaluator != NULL) {
		pstate->prhs_evaluator->pfree_func(pstate->prhs_evaluator);
	}

	free(pstate);
}

// ----------------------------------------------------------------
static void handle_local_variable_definition( // xxx mapvar
	mlr_dsl_cst_statement_t* pstatement,
	variables_t*             pvars,
	cst_outputs_t*           pcst_outputs)
{
	local_variable_definition_state_t* pstate = pstatement->pvstate;

	rval_evaluator_t* prhs_evaluator = pstate->prhs_evaluator; // xxx mapvar
	mv_t val = prhs_evaluator->pprocess_func(prhs_evaluator->pvstate, pvars);
	if (mv_is_present(&val)) {
		local_stack_frame_t* pframe = local_stack_get_top_frame(pvars->plocal_stack);
		local_stack_frame_define(pframe,
			pstate->lhs_variable_name, pstate->lhs_frame_relative_index,
			pstate->lhs_type_mask, val);
	} else {
		mv_free(&val);
	}
}

// ----------------------------------------------------------------
static void handle_local_variable_definition_from_map_literal( // xxx mapvar
	mlr_dsl_cst_statement_t* pstatement,
	variables_t*             pvars,
	cst_outputs_t*           pcst_outputs)
{
	local_variable_definition_state_t* pstate = pstatement->pvstate;

	//rval_evaluator_t* prhs_evaluator = pstate->prhs_evaluator; // xxx mapvar
	//mv_t val = prhs_evaluator->pprocess_func(prhs_evaluator->pvstate, pvars);
	// xxx mapvar stub
	mv_t val = mv_absent();
	//if (mv_is_present(&val)) {
		local_stack_frame_t* pframe = local_stack_get_top_frame(pvars->plocal_stack);
		local_stack_frame_define(pframe,
			pstate->lhs_variable_name, pstate->lhs_frame_relative_index,
			/*pstate->lhs_type_mask*/ TYPE_MASK_MAP|TYPE_MASK_ABSENT, val); // xxx temp stub
	//} else {
		//mv_free(&val);
	//}
}

// ================================================================
typedef struct _nonindexed_local_variable_assignment_state_t {
	char*             lhs_variable_name; // For error messages only: stack-index is computed by stack-allocator:
	int               lhs_frame_relative_index;
	rval_evaluator_t* prhs_evaluator;
} nonindexed_local_variable_assignment_state_t;

static mlr_dsl_cst_statement_handler_t handle_nonindexed_local_variable_assignment;
static mlr_dsl_cst_statement_freer_t free_nonindexed_local_variable_assignment;

// ----------------------------------------------------------------
mlr_dsl_cst_statement_t* alloc_nonindexed_local_variable_assignment(mlr_dsl_cst_t* pcst, mlr_dsl_ast_node_t* pnode, // xxx XXX mapvars next
	int type_inferencing, int context_flags)
{
	nonindexed_local_variable_assignment_state_t* pstate = mlr_malloc_or_die(sizeof(
		nonindexed_local_variable_assignment_state_t));

	MLR_INTERNAL_CODING_ERROR_IF((pnode->pchildren == NULL) || (pnode->pchildren->length != 2));

	mlr_dsl_ast_node_t* pleft  = pnode->pchildren->phead->pvvalue;
	mlr_dsl_ast_node_t* pright = pnode->pchildren->phead->pnext->pvvalue;

	MLR_INTERNAL_CODING_ERROR_IF(pleft->type != MD_AST_NODE_TYPE_NONINDEXED_LOCAL_VARIABLE);
	MLR_INTERNAL_CODING_ERROR_IF(pleft->pchildren != NULL);

	// xxx XXX mapvar case: rhs is MAP_LITERAL
	// xxx XXX mapvar case: rhs is FULL_SREC
	// xxx XXX mapvar case: rhs is OOSVAR_KEYLIST
	// xxx XXX mapvar case: rhs is FULL_OOSVAR
	// xxx XXX mapvar case: rhs is NONINDEXED_LOCAL_VARIABLE
	// xxx XXX mapvar case: rhs is INDEXED_LOCAL_VARIABLE
	// xxx XXX mapvar case: rhs is FUNC_CALLSITE

	pstate->lhs_variable_name = pleft->text;
	MLR_INTERNAL_CODING_ERROR_IF(pleft->vardef_frame_relative_index == MD_UNUSED_INDEX);
	pstate->lhs_frame_relative_index = pleft->vardef_frame_relative_index;
	pstate->prhs_evaluator = rval_evaluator_alloc_from_ast(pright, pcst->pfmgr, type_inferencing, context_flags);

	return mlr_dsl_cst_statement_valloc(
		pnode,
		handle_nonindexed_local_variable_assignment,
		free_nonindexed_local_variable_assignment,
		pstate);
}

// ----------------------------------------------------------------
static void free_nonindexed_local_variable_assignment(mlr_dsl_cst_statement_t* pstatement) {
	nonindexed_local_variable_assignment_state_t* pstate = pstatement->pvstate;

	pstate->prhs_evaluator->pfree_func(pstate->prhs_evaluator);

	free(pstate);
}

// ----------------------------------------------------------------
static void handle_nonindexed_local_variable_assignment(
	mlr_dsl_cst_statement_t* pstatement,
	variables_t*             pvars,
	cst_outputs_t*           pcst_outputs)
{
	nonindexed_local_variable_assignment_state_t* pstate = pstatement->pvstate;

	rval_evaluator_t* prhs_evaluator = pstate->prhs_evaluator;
	mv_t val = prhs_evaluator->pprocess_func(prhs_evaluator->pvstate, pvars);
	if (mv_is_present(&val)) {
		local_stack_frame_t* pframe = local_stack_get_top_frame(pvars->plocal_stack);
		local_stack_frame_assign_non_map(pframe, pstate->lhs_frame_relative_index, val);
	} else {
		mv_free(&val);
	}
}

// ================================================================
typedef struct _indexed_local_variable_assignment_state_t {
	char*             lhs_variable_name; // For error messages only: stack-index is computed by stack-allocator:
	int               lhs_frame_relative_index;
	sllv_t*           plhs_keylist_evaluators;
	rval_evaluator_t* prhs_evaluator;
} indexed_local_variable_assignment_state_t;

static mlr_dsl_cst_statement_handler_t handle_indexed_local_variable_assignment;
static mlr_dsl_cst_statement_freer_t free_indexed_local_variable_assignment;

// ----------------------------------------------------------------
mlr_dsl_cst_statement_t* alloc_indexed_local_variable_assignment(mlr_dsl_cst_t* pcst, mlr_dsl_ast_node_t* pnode,
	int type_inferencing, int context_flags)
{
	indexed_local_variable_assignment_state_t* pstate = mlr_malloc_or_die(sizeof(
		indexed_local_variable_assignment_state_t));

	mlr_dsl_ast_node_t* pleft  = pnode->pchildren->phead->pvvalue;
	mlr_dsl_ast_node_t* pright = pnode->pchildren->phead->pnext->pvvalue;

	MLR_INTERNAL_CODING_ERROR_IF(pleft->type != MD_AST_NODE_TYPE_INDEXED_LOCAL_VARIABLE);
	MLR_INTERNAL_CODING_ERROR_IF(pleft->pchildren == NULL);

	pstate->lhs_variable_name = mlr_strdup_or_die(pleft->text);
	MLR_INTERNAL_CODING_ERROR_IF(pleft->vardef_frame_relative_index == MD_UNUSED_INDEX);
	pstate->lhs_frame_relative_index = pleft->vardef_frame_relative_index;

	pstate->plhs_keylist_evaluators = allocate_keylist_evaluators_from_ast_node(
		pcst, pleft, type_inferencing, context_flags);

	switch(pright->type) { // xxx XXX mapvar cases

	case MD_AST_NODE_TYPE_MAP_LITERAL:
		// xxx
		break;

	case MD_AST_NODE_TYPE_FULL_SREC:
		break;

	case MD_AST_NODE_TYPE_OOSVAR_KEYLIST:
		break;

	case MD_AST_NODE_TYPE_FULL_OOSVAR:
		break;

	case MD_AST_NODE_TYPE_NONINDEXED_LOCAL_VARIABLE:
		break;

	case MD_AST_NODE_TYPE_INDEXED_LOCAL_VARIABLE:
		break;

	case MD_AST_NODE_TYPE_FUNC_CALLSITE:
		break;

	default:
		pstate->prhs_evaluator = rval_evaluator_alloc_from_ast(pright, pcst->pfmgr, type_inferencing, context_flags);
		break;

	}

	return mlr_dsl_cst_statement_valloc(
		pnode,
		handle_indexed_local_variable_assignment,
		free_indexed_local_variable_assignment,
		pstate);
}

// ----------------------------------------------------------------
static void free_indexed_local_variable_assignment(mlr_dsl_cst_statement_t* pstatement) {
	indexed_local_variable_assignment_state_t* pstate = pstatement->pvstate;

	for (sllve_t* pe = pstate->plhs_keylist_evaluators->phead; pe != NULL; pe = pe->pnext) {
		rval_evaluator_t* pev = pe->pvvalue;
		pev->pfree_func(pev);
	}
	pstate->prhs_evaluator->pfree_func(pstate->prhs_evaluator);

	free(pstate);
}

// ----------------------------------------------------------------
static void handle_indexed_local_variable_assignment(
	mlr_dsl_cst_statement_t* pstatement,
	variables_t*             pvars,
	cst_outputs_t*           pcst_outputs)
{
	indexed_local_variable_assignment_state_t* pstate = pstatement->pvstate;

	rval_evaluator_t* prhs_evaluator = pstate->prhs_evaluator;
	mv_t rhs_value = prhs_evaluator->pprocess_func(prhs_evaluator->pvstate, pvars);
	if (mv_is_present(&rhs_value)) {

		int all_non_null_or_error = TRUE;
		sllmv_t* pmvkeys = evaluate_list(pstate->plhs_keylist_evaluators, pvars,
			&all_non_null_or_error);
		if (all_non_null_or_error) {
			local_stack_frame_t* pframe = local_stack_get_top_frame(pvars->plocal_stack);
			local_stack_frame_assign_map(pframe, pstate->lhs_frame_relative_index, pmvkeys, rhs_value);
		}
		sllmv_free(pmvkeys);

	} else {
		mv_free(&rhs_value);
	}
}

// ================================================================
// All assignments produce a mlrval on the RHS and store it on the left -- except if both LHS and RHS
// are oosvars in which case there are recursive copies, or in case of $* on the LHS or RHS.

typedef struct _oosvar_assignment_state_t {
	sllv_t*           plhs_keylist_evaluators;
	rval_evaluator_t* prhs_evaluator;
	sllv_t*           prhs_keylist_evaluators;
} oosvar_assignment_state_t;

static mlr_dsl_cst_statement_handler_t handle_oosvar_assignment;
static mlr_dsl_cst_statement_handler_t handle_oosvar_to_oosvar_assignment;
static mlr_dsl_cst_statement_freer_t free_oosvar_assignment;

// ----------------------------------------------------------------
mlr_dsl_cst_statement_t* alloc_oosvar_assignment(mlr_dsl_cst_t* pcst, mlr_dsl_ast_node_t* pnode,
	int type_inferencing, int context_flags)
{
	oosvar_assignment_state_t* pstate = mlr_malloc_or_die(sizeof(oosvar_assignment_state_t));

	mlr_dsl_ast_node_t* pleft  = pnode->pchildren->phead->pvvalue;
	mlr_dsl_ast_node_t* pright = pnode->pchildren->phead->pnext->pvvalue;

	MLR_INTERNAL_CODING_ERROR_IF(pleft->type != MD_AST_NODE_TYPE_OOSVAR_KEYLIST);

	pstate->plhs_keylist_evaluators = allocate_keylist_evaluators_from_ast_node(pcst, pleft,
		type_inferencing, context_flags);

	mlr_dsl_cst_statement_handler_t* pstatement_handler = NULL;
	if (pleft->type == MD_AST_NODE_TYPE_OOSVAR_KEYLIST && pright->type == MD_AST_NODE_TYPE_OOSVAR_KEYLIST) {
		pstatement_handler = handle_oosvar_to_oosvar_assignment;
		pstate->prhs_keylist_evaluators = allocate_keylist_evaluators_from_ast_node(pcst, pright,
			type_inferencing, context_flags);
		pstate->prhs_evaluator = NULL;
	} else {
		pstatement_handler = handle_oosvar_assignment;
		pstate->prhs_keylist_evaluators = NULL;
		pstate->prhs_evaluator = rval_evaluator_alloc_from_ast(pright, pcst->pfmgr,
			type_inferencing, context_flags);
	}

	return mlr_dsl_cst_statement_valloc(
		pnode,
		pstatement_handler,
		free_oosvar_assignment,
		pstate);
}

// ----------------------------------------------------------------
static void free_oosvar_assignment(mlr_dsl_cst_statement_t* pstatement) {
	oosvar_assignment_state_t* pstate = pstatement->pvstate;

	for (sllve_t* pe = pstate->plhs_keylist_evaluators->phead; pe != NULL; pe = pe->pnext) {
		rval_evaluator_t* pev = pe->pvvalue;
		pev->pfree_func(pev);
	}
	if (pstate->prhs_evaluator != NULL) {
		pstate->prhs_evaluator->pfree_func(pstate->prhs_evaluator);
	}
	if (pstate->prhs_keylist_evaluators != NULL) {
		for (sllve_t* pe = pstate->prhs_keylist_evaluators->phead; pe != NULL; pe = pe->pnext) {
			rval_evaluator_t* pev = pe->pvvalue;
			pev->pfree_func(pev);
		}
	}

	free(pstate);
}

// ----------------------------------------------------------------
static void handle_oosvar_assignment(
	mlr_dsl_cst_statement_t* pstatement,
	variables_t*             pvars,
	cst_outputs_t*           pcst_outputs)
{
	oosvar_assignment_state_t* pstate = pstatement->pvstate;

	rval_evaluator_t* prhs_evaluator = pstate->prhs_evaluator;
	mv_t rhs_value = prhs_evaluator->pprocess_func(prhs_evaluator->pvstate, pvars);

	if (mv_is_present(&rhs_value)) {
		int all_non_null_or_error = TRUE;
		sllmv_t* pmvkeys = evaluate_list(pstate->plhs_keylist_evaluators, pvars,
			&all_non_null_or_error);
		if (all_non_null_or_error)
			mlhmmv_put_terminal(pvars->poosvars, pmvkeys, &rhs_value);
		sllmv_free(pmvkeys);
	}
	mv_free(&rhs_value);
}

// ----------------------------------------------------------------
static void handle_oosvar_to_oosvar_assignment(
	mlr_dsl_cst_statement_t* pstatement,
	variables_t*             pvars,
	cst_outputs_t*           pcst_outputs)
{
	oosvar_assignment_state_t* pstate = pstatement->pvstate;

	int lhs_all_non_null_or_error = TRUE;
	sllmv_t* plhskeys = evaluate_list(pstate->plhs_keylist_evaluators, pvars,
		&lhs_all_non_null_or_error);

	if (lhs_all_non_null_or_error) {
		int rhs_all_non_null_or_error = TRUE;
		sllmv_t* prhskeys = evaluate_list(pstate->prhs_keylist_evaluators, pvars,
			&rhs_all_non_null_or_error);
		if (rhs_all_non_null_or_error) {
			mlhmmv_copy(pvars->poosvars, plhskeys, prhskeys);
		}
		sllmv_free(prhskeys);
	}

	sllmv_free(plhskeys);
}

// ================================================================
// All assignments produce a mlrval on the RHS and store it on the left -- except if both LHS and RHS
// are oosvars in which case there are recursive copies, or in case of $* on the LHS or RHS.

typedef struct _oosvar_from_full_srec_assignment_state_t {
	sllv_t* plhs_keylist_evaluators;
} oosvar_from_full_srec_assignment_state_t;

static mlr_dsl_cst_statement_handler_t handle_oosvar_from_full_srec_assignment;
static mlr_dsl_cst_statement_freer_t free_oosvar_from_full_srec_assignment;

// ----------------------------------------------------------------
mlr_dsl_cst_statement_t* alloc_oosvar_from_full_srec_assignment(mlr_dsl_cst_t* pcst, mlr_dsl_ast_node_t* pnode,
	int type_inferencing, int context_flags)
{
	oosvar_from_full_srec_assignment_state_t* pstate = mlr_malloc_or_die(sizeof(
		oosvar_from_full_srec_assignment_state_t));

	mlr_dsl_ast_node_t* pleft  = pnode->pchildren->phead->pvvalue;
	mlr_dsl_ast_node_t* pright = pnode->pchildren->phead->pnext->pvvalue;

	MLR_INTERNAL_CODING_ERROR_IF(pleft->type != MD_AST_NODE_TYPE_OOSVAR_KEYLIST);
	MLR_INTERNAL_CODING_ERROR_IF(pright->type != MD_AST_NODE_TYPE_FULL_SREC);

	pstate->plhs_keylist_evaluators = allocate_keylist_evaluators_from_ast_node(pcst, pleft,
		type_inferencing, context_flags);

	return mlr_dsl_cst_statement_valloc(
		pnode,
		handle_oosvar_from_full_srec_assignment,
		free_oosvar_from_full_srec_assignment,
		pstate);
}

// ----------------------------------------------------------------
static void free_oosvar_from_full_srec_assignment(mlr_dsl_cst_statement_t* pstatement) {
	oosvar_from_full_srec_assignment_state_t* pstate = pstatement->pvstate;

	for (sllve_t* pe = pstate->plhs_keylist_evaluators->phead; pe != NULL; pe = pe->pnext) {
		rval_evaluator_t* pev = pe->pvvalue;
		pev->pfree_func(pev);
	}

	free(pstate);
}

// ----------------------------------------------------------------
static void handle_oosvar_from_full_srec_assignment(
	mlr_dsl_cst_statement_t* pstatement,
	variables_t*             pvars,
	cst_outputs_t*           pcst_outputs)
{
	oosvar_from_full_srec_assignment_state_t* pstate = pstatement->pvstate;

	int all_non_null_or_error = TRUE;
	sllmv_t* plhskeys = evaluate_list(pstate->plhs_keylist_evaluators, pvars, &all_non_null_or_error);
	if (all_non_null_or_error) {

		mlhmmv_level_t* plevel = mlhmmv_get_or_create_level(pvars->poosvars, plhskeys);
		if (plevel != NULL) {

			mlhmmv_clear_level(plevel);

			for (lrece_t* pe = pvars->pinrec->phead; pe != NULL; pe = pe->pnext) {
				mv_t k = mv_from_string(pe->key, NO_FREE); // mlhmmv_put_terminal_from_level will copy
				sllmve_t e = { .value = k, .free_flags = 0, .pnext = NULL };
				mv_t* pomv = lhmsmv_get(pvars->ptyped_overlay, pe->key);
				if (pomv != NULL) {
					mlhmmv_put_terminal_from_level(plevel, &e, pomv);
				} else {
					mv_t v = mv_from_string(pe->value, NO_FREE); // mlhmmv_put_terminal_from_level will copy
					mlhmmv_put_terminal_from_level(plevel, &e, &v);
				}
			}

		}
	}
	sllmv_free(plhskeys);
}

// ================================================================
// All assignments produce a mlrval on the RHS and store it on the left -- except if both LHS and RHS
// are oosvars in which case there are recursive copies, or in case of $* on the LHS or RHS.

typedef struct _full_srec_from_oosvar_assignment_state_t {
	sllv_t* prhs_keylist_evaluators;
} full_srec_from_oosvar_assignment_state_t;

static mlr_dsl_cst_statement_handler_t handle_full_srec_from_oosvar_assignment;
static mlr_dsl_cst_statement_freer_t free_full_srec_from_oosvar_assignment;

// ----------------------------------------------------------------
mlr_dsl_cst_statement_t* alloc_full_srec_from_oosvar_assignment(mlr_dsl_cst_t* pcst, mlr_dsl_ast_node_t* pnode,
	int type_inferencing, int context_flags)
{
	full_srec_from_oosvar_assignment_state_t* pstate = mlr_malloc_or_die(sizeof(
		full_srec_from_oosvar_assignment_state_t));

	mlr_dsl_ast_node_t* pleft  = pnode->pchildren->phead->pvvalue;
	mlr_dsl_ast_node_t* pright = pnode->pchildren->phead->pnext->pvvalue;

	MLR_INTERNAL_CODING_ERROR_IF(pleft->type != MD_AST_NODE_TYPE_FULL_SREC);
	MLR_INTERNAL_CODING_ERROR_IF(pright->type != MD_AST_NODE_TYPE_OOSVAR_KEYLIST);

	pstate->prhs_keylist_evaluators = allocate_keylist_evaluators_from_ast_node(pcst, pright,
		type_inferencing, context_flags);

	return mlr_dsl_cst_statement_valloc(
		pnode,
		handle_full_srec_from_oosvar_assignment,
		free_full_srec_from_oosvar_assignment,
		pstate);
}

// ----------------------------------------------------------------
static void free_full_srec_from_oosvar_assignment(mlr_dsl_cst_statement_t* pstatement) {
	full_srec_from_oosvar_assignment_state_t* pstate = pstatement->pvstate;

	for (sllve_t* pe = pstate->prhs_keylist_evaluators->phead; pe != NULL; pe = pe->pnext) {
		rval_evaluator_t* pev = pe->pvvalue;
		pev->pfree_func(pev);
	}

	free(pstate);
}

// ----------------------------------------------------------------
static void handle_full_srec_from_oosvar_assignment(
	mlr_dsl_cst_statement_t* pstatement,
	variables_t*             pvars,
	cst_outputs_t*           pcst_outputs)
{
	full_srec_from_oosvar_assignment_state_t* pstate = pstatement->pvstate;

	lrec_clear(pvars->pinrec);
	lhmsmv_clear(pvars->ptyped_overlay);

	int all_non_null_or_error = TRUE;
	sllmv_t* prhskeys = evaluate_list(pstate->prhs_keylist_evaluators, pvars, &all_non_null_or_error);
	if (all_non_null_or_error) {
		int error = 0;
		mlhmmv_level_t* plevel = mlhmmv_get_level(pvars->poosvars, prhskeys, &error);
		if (plevel != NULL) {
			for (mlhmmv_level_entry_t* pentry = plevel->phead; pentry != NULL; pentry = pentry->pnext) {
				if (pentry->level_value.is_terminal) {
					char* skey = mv_alloc_format_val(&pentry->level_key);
					mv_t val = mv_copy(&pentry->level_value.u.mlrval);

					// Write typed mlrval output to the typed overlay rather than into the lrec
					// (which holds only string values).
					//
					// The rval_evaluator reads the overlay in preference to the lrec. E.g. if the
					// input had "x"=>"abc","y"=>"def" but a previous statement had set "y"=>7.4 and
					// "z"=>"ghi", then an expression right-hand side referring to $y would get the
					// floating-point value 7.4. So we don't need to lrec_put the value here, and
					// moreover should not for two reasons: (1) there is a performance hit of doing
					// throwaway number-to-string formatting -- it's better to do it once at the
					// end; (2) having the string values doubly owned by the typed overlay and the
					// lrec would result in double frees, or awkward bookkeeping. However, the NR
					// variable evaluator reads prec->field_count, so we need to put something here.
					// And putting something statically allocated minimizes copying/freeing.
					lhmsmv_put(pvars->ptyped_overlay, mlr_strdup_or_die(skey), &val,
						FREE_ENTRY_KEY | FREE_ENTRY_VALUE);
					lrec_put(pvars->pinrec, skey, "bug", FREE_ENTRY_KEY);
				}
			}
		}
	}
	sllmv_free(prhskeys);
}

// ================================================================
typedef struct _env_assignment_state_t {
	rval_evaluator_t* plhs_evaluator;
	rval_evaluator_t* prhs_evaluator;
} env_assignment_state_t;

static mlr_dsl_cst_statement_handler_t handle_env_assignment;
static mlr_dsl_cst_statement_freer_t free_env_assignment;

// ----------------------------------------------------------------
mlr_dsl_cst_statement_t* alloc_env_assignment(mlr_dsl_cst_t* pcst, mlr_dsl_ast_node_t* pnode,
	int type_inferencing, int context_flags)
{
	env_assignment_state_t* pstate = mlr_malloc_or_die(sizeof(env_assignment_state_t));

	MLR_INTERNAL_CODING_ERROR_IF((pnode->pchildren == NULL) || (pnode->pchildren->length != 2));

	mlr_dsl_ast_node_t* pleft  = pnode->pchildren->phead->pvvalue;
	mlr_dsl_ast_node_t* pright = pnode->pchildren->phead->pnext->pvvalue;

	MLR_INTERNAL_CODING_ERROR_IF(pleft->type != MD_AST_NODE_TYPE_ENV);
	MLR_INTERNAL_CODING_ERROR_IF(pleft->pchildren == NULL);
	MLR_INTERNAL_CODING_ERROR_IF(pleft->pchildren->length != 2);
	mlr_dsl_ast_node_t* pnamenode  = pleft->pchildren->phead->pnext->pvvalue;

	pstate->plhs_evaluator = rval_evaluator_alloc_from_ast(pnamenode, pcst->pfmgr, type_inferencing, context_flags);
	pstate->prhs_evaluator = rval_evaluator_alloc_from_ast(pright, pcst->pfmgr, type_inferencing, context_flags);

	return mlr_dsl_cst_statement_valloc(
		pnode,
		handle_env_assignment,
		free_env_assignment,
		pstate);
}

// ----------------------------------------------------------------
static void free_env_assignment(mlr_dsl_cst_statement_t* pstatement) {
	env_assignment_state_t* pstate = pstatement->pvstate;

	pstate->plhs_evaluator->pfree_func(pstate->plhs_evaluator);
	pstate->prhs_evaluator->pfree_func(pstate->prhs_evaluator);

	free(pstate);
}

// ----------------------------------------------------------------
static void handle_env_assignment(
	mlr_dsl_cst_statement_t* pstatement,
	variables_t*             pvars,
	cst_outputs_t*           pcst_outputs)
{
	env_assignment_state_t* pstate = pstatement->pvstate;

	rval_evaluator_t* plhs_evaluator = pstate->plhs_evaluator;
	rval_evaluator_t* prhs_evaluator = pstate->prhs_evaluator;
	mv_t lval = plhs_evaluator->pprocess_func(plhs_evaluator->pvstate, pvars);
	mv_t rval = prhs_evaluator->pprocess_func(prhs_evaluator->pvstate, pvars);

	if (mv_is_present(&lval) && mv_is_present(&rval)) {
		setenv(mlr_strdup_or_die(mv_alloc_format_val(&lval)), mlr_strdup_or_die(mv_alloc_format_val(&rval)), 1);
	}
	mv_free(&lval);
	mv_free(&rval);
}
