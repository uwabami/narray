/*
  ndloop.c
  Numerical Array Extension for Ruby
    (C) Copyright 1999-2016 by Masahiro TANAKA

  This program is free software.
  You can distribute/modify this program
  under the same terms as Ruby itself.
  NO WARRANTY.
*/

#include <ruby.h>
#include "numo/narray.h"

#if 0
#define DBG(x) x
#else
#define DBG(x)
#endif

#ifdef HAVE_STDARG_PROTOTYPES
#include <stdarg.h>
#define va_init_list(a,b) va_start(a,b)
#else
#include <varargs.h>
#define va_init_list(a,b) va_start(a)
#endif

typedef struct NA_BUFFER_COPY {
    int ndim;
    size_t elmsz;
    size_t *n;
    char *src_ptr;
    char *buf_ptr;
    na_loop_iter_t *src_iter;
    na_loop_iter_t *buf_iter;
} na_buffer_copy_t;

typedef struct NA_LOOP_XARGS {
    na_loop_iter_t *iter;     // moved from na_loop_t
    na_buffer_copy_t *bufcp;  // copy data to buffer
    int flag;                 // NDL_READ NDL_WRITE
    boolean free_user_iter;   // alloc LARG(lp,j).iter=lp->xargs[j].iter
} na_loop_xargs_t;

typedef struct NA_MD_LOOP {
    int  narg;
    int  nin;
    int  ndim;                // n of total dimention
    unsigned int copy_flag;   // set i-th bit if i-th arg is cast
    size_t  *n_ptr;           // memory for n
    na_loop_iter_t *iter_ptr; // memory for iter
    size_t  *n;               // n of elements for each dim
    na_loop_t  user;          // loop in user function
    na_loop_xargs_t *xargs;   // extra data for each arg
    int    writeback;         // write back result to i-th arg
    int    init_aidx;         // index of initializer argument
    int    reduce_dim;
    int   *trans_map;
    VALUE  vargs;
    VALUE  reduce;
    VALUE  loop_opt;
    ndfunc_t  *ndfunc;
    void (*loop_func)();
} na_md_loop_t;

#define LARG(lp,iarg) ((lp)->user.args[iarg])
#define LITER(lp,idim,iarg) ((lp)->xargs[iarg].iter[idim])
#define LITER_SRC(lp,idim) ((lp)->src_iter[idim])
#define LBUFCP(lp,j) ((lp)->xargs[j].bufcp)

#define CASTABLE(t) (RTEST(t) && (t)!=OVERWRITE)

#define NDL_READ 1
#define NDL_WRITE 2
#define NDL_READ_WRITE 3

static inline VALUE
nary_type_s_cast(VALUE type, VALUE obj)
{
    return rb_funcall(type,rb_intern("cast"),1,obj);
}

static void
print_ndfunc(ndfunc_t *nf) {
    volatile VALUE t;
    int i, k;
    printf("ndfunc_t = 0x%"SZF"x {\n",(size_t)nf);
    printf("  func  = 0x%"SZF"x\n", (size_t)nf->func);
    printf("  flag  = 0x%"SZF"x\n", (size_t)nf->flag);
    printf("  nin   = %d\n", nf->nin);
    printf("  nout  = %d\n", nf->nout);
    printf("  ain   = 0x%"SZF"x\n", (size_t)nf->ain);
    for (i=0; i<nf->nin; i++) {
        t = rb_inspect(nf->ain[i].type);
        printf("  ain[%d].type = %s\n", i, StringValuePtr(t));
        printf("  ain[%d].dim = %d\n", i, nf->ain[i].dim);
    }
    printf("  aout  = 0x%"SZF"x\n", (size_t)nf->aout);
    for (i=0; i<nf->nout; i++) {
        t = rb_inspect(nf->aout[i].type);
        printf("  aout[%d].type = %s\n", i, StringValuePtr(t));
        printf("  aout[%d].dim = %d\n", i, nf->aout[i].dim);
        for (k=0; k<nf->aout[i].dim; k++) {
            printf("  aout[%d].shape[%d] = %"SZF"u\n", i, k, nf->aout[i].shape[k]);
        }
    }
    printf("}\n");
}


static void
print_ndloop(na_md_loop_t *lp) {
    int i,j,nd;
    printf("na_md_loop_t = 0x%"SZF"x {\n",(size_t)lp);
    printf("  narg = %d\n", lp->narg);
    printf("  nin  = %d\n", lp->nin);
    printf("  ndim = %d\n", lp->ndim);
    printf("  copy_flag = %x\n", lp->copy_flag);
    printf("  writeback = %d\n", lp->writeback);
    printf("  init_aidx = %d\n", lp->init_aidx);
    printf("  reduce_dim = %d\n", lp->reduce_dim);
    printf("  trans_map = 0x%"SZF"x\n", (size_t)lp->trans_map);
    nd = lp->ndim + lp->user.ndim;
    for (i=0; i<nd; i++) {
        printf("  trans_map[%d] = %d\n", i, lp->trans_map[i]);
    }
    printf("  n = 0x%"SZF"x\n", (size_t)lp->n);
    nd = lp->ndim + lp->user.ndim;
    for (i=0; i<=lp->ndim; i++) {
        printf("  n[%d] = %"SZF"u\n", i, lp->n[i]);
    }
    printf("  user.n = 0x%"SZF"x\n", (size_t)lp->user.n);
    if (lp->user.n) {
        for (i=0; i<=lp->user.ndim; i++) {
            printf("  user.n[%d] = %"SZF"u\n", i, lp->user.n[i]);
        }
    }
    printf("  xargs = 0x%"SZF"x\n", (size_t)lp->xargs);
    printf("  iter_ptr = 0x%"SZF"x\n", (size_t)lp->iter_ptr);
    printf("  user.narg = %d\n", lp->user.narg);
    printf("  user.ndim = %d\n", lp->user.ndim);
    printf("  user.args = 0x%"SZF"x\n", (size_t)lp->user.args);
    for (j=0; j<lp->narg; j++) {
    }
    printf("  user.opt_ptr = 0x%"SZF"x\n", (size_t)lp->user.opt_ptr);
    if (lp->reduce==Qnil) {
        printf("  reduce  = nil\n");
    } else {
        printf("  reduce  = 0x%x\n", NUM2INT(lp->reduce));
    }
    for (j=0; j<lp->narg; j++) {
        printf("--user.args[%d]--\n", j);
        printf("  user.args[%d].ptr = 0x%"SZF"x\n", j, (size_t)LARG(lp,j).ptr);
        printf("  user.args[%d].elmsz = %"SZF"d\n", j, LARG(lp,j).elmsz);
        printf("  user.args[%d].value = 0x%"SZF"x\n", j, LARG(lp,j).value);
        printf("  user.args[%d].ndim = %d\n", j, LARG(lp,j).ndim);
        printf("  user.args[%d].shape = 0x%"SZF"x\n", j, (size_t)LARG(lp,j).shape);
        if (LARG(lp,j).shape) {
            for (i=0; i<LARG(lp,j).ndim; i++) {
                printf("  user.args[%d].shape[%d] = %"SZF"d\n", j, i, LARG(lp,j).shape[i]);
            }
        }
        printf("  user.args[%d].iter = 0x%"SZF"x\n", j,(size_t)lp->user.args[j].iter);
        if (lp->user.args[j].iter) {
            for (i=0; i<lp->user.ndim; i++) {
                printf(" &user.args[%d].iter[%d] = 0x%"SZF"x\n", j,i, (size_t)&lp->user.args[j].iter[i]);
                printf("  user.args[%d].iter[%d].pos = %"SZF"u\n", j,i, lp->user.args[j].iter[i].pos);
                printf("  user.args[%d].iter[%d].step = %"SZF"u\n", j,i, lp->user.args[j].iter[i].step);
                printf("  user.args[%d].iter[%d].idx = 0x%"SZF"x\n", j,i, (size_t)lp->user.args[j].iter[i].idx);
            }
        }
        //
        printf("  xargs[%d].flag = %d\n", j, lp->xargs[j].flag);
        printf("  xargs[%d].free_user_iter = %d\n", j, lp->xargs[j].free_user_iter);
        for (i=0; i<=nd; i++) {
            printf(" &xargs[%d].iter[%d] = 0x%"SZF"x\n", j,i, (size_t)&LITER(lp,i,j));
            printf("  xargs[%d].iter[%d].pos = %"SZF"u\n", j,i, LITER(lp,i,j).pos);
            printf("  xargs[%d].iter[%d].step = %"SZF"u\n", j,i, LITER(lp,i,j).step);
            printf("  xargs[%d].iter[%d].idx = 0x%"SZF"x\n", j,i, (size_t)LITER(lp,i,j).idx);
        }
        printf("  xargs[%d].bufcp = 0x%"SZF"x\n", j, (size_t)lp->xargs[j].bufcp);
        if (lp->xargs[j].bufcp) {
            printf("  xargs[%d].bufcp->ndim = %d\n", j, lp->xargs[j].bufcp->ndim);
            printf("  xargs[%d].bufcp->elmsz = %"SZF"d\n", j, lp->xargs[j].bufcp->elmsz);
            printf("  xargs[%d].bufcp->n = 0x%"SZF"x\n", j, (size_t)lp->xargs[j].bufcp->n);
            printf("  xargs[%d].bufcp->src_ptr = 0x%"SZF"x\n", j, (size_t)lp->xargs[j].bufcp->src_ptr);
            printf("  xargs[%d].bufcp->buf_ptr = 0x%"SZF"x\n", j, (size_t)lp->xargs[j].bufcp->buf_ptr);
            printf("  xargs[%d].bufcp->src_iter = 0x%"SZF"x\n", j, (size_t)lp->xargs[j].bufcp->src_iter);
            printf("  xargs[%d].bufcp->buf_iter = 0x%"SZF"x\n", j, (size_t)lp->xargs[j].bufcp->buf_iter);
        }
    }
    printf("}\n");
}


static unsigned int
ndloop_func_loop_spec(ndfunc_t *nf, int user_ndim)
{
    unsigned int f=0;
    // If user function supports LOOP
    if (user_ndim > 0 || NDF_TEST(nf,NDF_HAS_LOOP)) {
        if (!NDF_TEST(nf,NDF_STRIDE_LOOP)) {
            f |= 1;
        }
        if (!NDF_TEST(nf,NDF_INDEX_LOOP)) {
            f |= 2;
        }
    }
    return f;
}




// convert input argeuments given by RARRAY_PTR(args)[j]
//              to type specified by nf->args[j].type
// returns copy_flag where nth-bit is set if nth argument is converted.
static unsigned int
ndloop_cast_args(ndfunc_t *nf, VALUE args)
{
    int j;
    char *s;
    unsigned int copy_flag=0;
    volatile VALUE v, t, x;

    //if (na_debug_flag) rb_p(args);

    for (j=0; j<nf->nin; j++) {
        t = nf->ain[j].type;
        //x = rb_inspect(t);
        //s = StringValueCStr(x);
        //printf("TYPE(nf->ain[%d].type) = %d, t = nf->ain[%d].type=%s\n",j,TYPE(t),j,s);
        if (TYPE(t)!=T_SYMBOL) {
            // argument
            v = RARRAY_AREF(args,j);
            //x = rb_inspect(v);
            //s = StringValueCStr(x);
            //printf(" v = RARRAY_AREF(args,%d) = %s\n", j, s);
            // skip cast if type is nil or same as input value
            if (CASTABLE(t) && t != CLASS_OF(v)) {
                // else do cast
                if (rb_obj_is_kind_of(t, rb_cClass)) {
                    if (RTEST(rb_class_inherited_p(t, cNArray))) {
                        v = nary_type_s_cast(t, v);
                        RARRAY_ASET(args,j,v);
                        copy_flag |= 1<<j;
                        //x = rb_inspect(t);
                        //s = StringValueCStr(x);
                        //printf(" nary_type_s_cast(t, v) = %s\n", s);
                        continue;
                    }
                }
                x = rb_inspect(t);
                s = StringValueCStr(x);
                rb_bug("fail cast from %s to %s", rb_obj_classname(v),s);
                rb_raise(rb_eTypeError,"fail cast from %s to %s",
                         rb_obj_classname(v), s);
            }
        }
    }
    return copy_flag;
}


/*
  user-dimension:
    user_nd = MAX( nf->args[j].dim )

  user-support dimension:

  loop dimension:
    loop_nd
*/

static void
ndloop_alloc(na_md_loop_t *lp, ndfunc_t *nf, VALUE args,
             void *opt_ptr, unsigned int copy_flag,
             void (*loop_func)(ndfunc_t*, na_md_loop_t*))
{
    int i,j;
    int narg;
    int user_nd, loop_nd, max_nd, tmp_nd;
    VALUE v, t;
    narray_t *na;

    int nin, nout, nopt;
    long args_len;

    na_loop_iter_t *iter;

    int trans_dim;
    unsigned int f;

    args_len = RARRAY_LEN(args);

    if (args_len != nf->nin) {
        rb_bug("wrong number of arguments for ndfunc (%lu for %d)",
               args_len, nf->nin);
    }

    lp->vargs = args;
    lp->ndfunc = nf;
    lp->loop_func = loop_func;
    lp->copy_flag = copy_flag;

    lp->reduce = Qnil;
    lp->user.option = Qnil;
    lp->user.opt_ptr = opt_ptr;
    lp->user.err_type = Qfalse;
    lp->loop_opt = Qnil;
    lp->writeback = -1;
    lp->init_aidx = -1;

    lp->n = NULL;
    lp->n_ptr = NULL;
    lp->xargs = NULL;
    lp->user.args = NULL;
    lp->user.n = NULL;
    lp->iter_ptr = NULL;
    lp->trans_map = NULL;

    nout = nf->nout;
    nin = 0;
    nopt = 0;

    // find max dimension, symbol options
    user_nd = 0;
    loop_nd = 0;
    for (j=0; j<args_len; j++) {
        t = nf->ain[j].type;
        v = RARRAY_AREF(args,j);
        // Symbol
        if (TYPE(t)==T_SYMBOL) {
            nopt++;
            if (t==sym_reduce) {
                lp->reduce = v;
            }
            else if (t==sym_option) {
                lp->user.option = v;
            }
            else if (t==sym_loop_opt) {
                lp->loop_opt = v;
            }
            else if (t==sym_init) {
                lp->init_aidx = j;
            }
            else {
                rb_bug("ndloop parse_options: unknown type");
            }
        } else {
            nin++;
            // max dimension of user function
            tmp_nd = nf->ain[j].dim;
            if (tmp_nd > user_nd) {
                user_nd = tmp_nd;
            }
            // max dimension of md-loop
            v = RARRAY_AREF(args,j);
            if (IsNArray(v)) {
                GetNArray(v,na);
                // array-dimension minus user-dimension
                tmp_nd = na->ndim - nf->ain[j].dim;
                if (tmp_nd > loop_nd) {
                    loop_nd = tmp_nd;
                }
            }
        }
    }

    if (user_nd>0 && RTEST(lp->reduce)) {
        rb_bug("reduce dimension conflicts with user dimension");
    }

    narg = nin + nout;
    max_nd = loop_nd + user_nd;

    lp->nin = nin;
    lp->narg = narg;
    lp->ndim = loop_nd;
    lp->user.narg = narg;
    lp->user.ndim = user_nd;

    lp->n    = lp->n_ptr = ALLOC_N(size_t, max_nd+1);
    lp->xargs = ALLOC_N(na_loop_xargs_t, narg);
    lp->user.args = ALLOC_N(na_loop_args_t, narg);
    iter = ALLOC_N(na_loop_iter_t, narg*(max_nd+1));
    lp->iter_ptr = iter;

    for (j=0; j<narg; j++) {
        LARG(lp,j).value = Qnil;
        LARG(lp,j).iter = NULL;
        LARG(lp,j).shape = NULL;
        LARG(lp,j).ndim = 0;
        lp->xargs[j].iter = &(iter[(max_nd+1)*j]);
        lp->xargs[j].bufcp = NULL;
        lp->xargs[j].flag = (j<nin) ? NDL_READ : NDL_WRITE;
        lp->xargs[j].free_user_iter = 0;
    }

    for (i=0; i<=max_nd; i++) {
        lp->n[i] = 1;
        for (j=0; j<narg; j++) {
            LITER(lp,i,j).pos = 0;
            LITER(lp,i,j).step = 0;
            LITER(lp,i,j).idx = NULL;
        }
    }

    // transpose reduce-dimensions to last dimensions
    //              array          loop
    //           [*,+,*,+,*] => [*,*,*,+,+]
    // trans_map=[0,3,1,4,2] <= [0,1,2,3,4]
    lp->trans_map = ALLOC_N(int, max_nd+1);
    if (NDF_TEST(nf,NDF_FLAT_REDUCE) && RTEST(lp->reduce)) {
        trans_dim = 0;
        for (i=0; i<max_nd; i++) {
            if (na_test_reduce(lp->reduce, i)) {
                lp->trans_map[i] = -1;
            } else {
                lp->trans_map[i] = trans_dim++;
            }
        }
        j = trans_dim;
        for (i=0; i<max_nd; i++) {
            if (lp->trans_map[i] == -1) {
                lp->trans_map[i] = j++;
            }
        }
        lp->reduce_dim = max_nd - trans_dim;
        f = 0;
        for (i=trans_dim; i<max_nd; i++) {
            f |= 1<<i;
        }
        lp->reduce = INT2FIX(f);
    } else {
        for (i=0; i<max_nd; i++) {
            lp->trans_map[i] = i;
        }
        lp->reduce_dim = 0;
    }
}


static VALUE
ndloop_release(VALUE vlp)
{
    int j;
    VALUE v;
    na_md_loop_t *lp = (na_md_loop_t*)(vlp);

    for (j=0; j < lp->narg; j++) {
        v = LARG(lp,j).value;
        if (IsNArray(v)) {
            na_release_lock(v);
        }
    }
    //xfree(lp);
    for (j=0; j<lp->narg; j++) {
        //printf("lp->xargs[%d].bufcp=%lx\n",j,(size_t)(lp->xargs[j].bufcp));
        if (lp->xargs[j].bufcp) {
            xfree(lp->xargs[j].bufcp->buf_iter);
            xfree(lp->xargs[j].bufcp->buf_ptr);
            xfree(lp->xargs[j].bufcp->n);
            xfree(lp->xargs[j].bufcp);
            if (lp->xargs[j].free_user_iter) {
                xfree(LARG(lp,j).iter);
            }
        }
    }
    if (lp->trans_map) xfree(lp->trans_map);
    xfree(lp->xargs);
    xfree(lp->iter_ptr);
    xfree(lp->user.args);
    xfree(lp->n_ptr);
    //rb_gc_force_recycle(vlp);
    return Qnil;
}


/*
static void
ndloop_free(na_md_loop_t* lp)
{
    int j;
    VALUE v;

    for (j=0; j<lp->narg; j++) {
        v = LARG(lp,j).value;
        if (IsNArray(v)) {
            na_release_lock(v);
        }
    }
    xfree(lp);
}
*/


/*
  set lp->n[i] (shape of n-d iteration) here
*/
static void
ndloop_check_shape(na_md_loop_t *lp, int nf_dim, narray_t *na)
{
    int i, k;
    size_t n;
    int dim_beg;

    dim_beg = lp->ndim + nf_dim - na->ndim;

    for (k = na->ndim - nf_dim - 1; k>=0; k--) {
        i = lp->trans_map[k + dim_beg];
        n = na->shape[k];
        // if n==1 then repeat this dimension
        if (n != 1) {
            if (lp->n[i] == 1) {
                lp->n[i] = n;
            } else if (lp->n[i] != n) {
                // inconsistent array shape
                rb_raise(rb_eTypeError,"shape1[%d](=%"SZF"u) != shape2[%d](=%"SZF"u)",
                         i, lp->n[i], k, n);
            }
        }
    }
}


/*
na->shape[i] == lp->n[ dim_map[i] ]
 */
static void
ndloop_set_stepidx(na_md_loop_t *lp, int j, VALUE vna, int *dim_map, int rwflag)
{
    size_t n, s;
    int i, k;
    stridx_t sdx;
    narray_t *na;

    LARG(lp,j).value = vna;
    LARG(lp,j).elmsz = na_get_elmsz(vna);
    if (rwflag & NDL_READ) {
        LARG(lp,j).ptr = na_get_pointer_for_read(vna);
    } else
    if (rwflag & NDL_WRITE) {
        LARG(lp,j).ptr = na_get_pointer_for_write(vna);
    } else {
        rb_bug("invalid value for read-write flag");
    }
    GetNArray(vna,na);

    switch(NA_TYPE(na)) {
    case NARRAY_DATA_T:
        if (NA_DATA_PTR(na)==NULL && NA_SIZE(na)>0) {
            rb_bug("cannot read no-data NArray");
            rb_raise(rb_eRuntimeError,"cannot read no-data NArray");
        }
        // through
    case NARRAY_FILEMAP_T:
        s = LARG(lp,j).elmsz;
        for (k=na->ndim; k--;) {
            n = na->shape[k];
            if (n > 1) {
                i = dim_map[k];
                //printf("n=%d k=%d i=%d\n",n,k,i);
                LITER(lp,i,j).step = s;
                LITER(lp,i,j).idx = NULL;
            }
            s *= n;
        }
        LITER(lp,0,j).pos = 0;
        break;
    case NARRAY_VIEW_T:
        for (k=0; k<na->ndim; k++) {
            n = na->shape[k];
            if (n > 1) {
                i = dim_map[k];
                sdx = NA_VIEW_STRIDX(na)[k];
                if (SDX_IS_INDEX(sdx)) {
                    LITER(lp,i,j).step = 0;
                    LITER(lp,i,j).idx = SDX_GET_INDEX(sdx);
                } else {
                    LITER(lp,i,j).step = SDX_GET_STRIDE(sdx);
                    LITER(lp,i,j).idx = NULL;
                }
            }
        }
        LITER(lp,0,j).pos = NA_VIEW_OFFSET(na);
        break;
    default:
        rb_bug("invalid narray internal type");
    }
}



static void
ndloop_init_args(ndfunc_t *nf, na_md_loop_t *lp, VALUE args)
{
    int i, j;
    VALUE v;
    narray_t *na;
    int nf_dim;
    int dim_beg;
    int *dim_map;
    int max_nd = lp->ndim + lp->user.ndim;
    int flag;
    size_t s;

/*
na->shape[i] == lp->n[ dim_map[i] ]
 */
    dim_map = ALLOCA_N(int, max_nd);

    // input arguments
    for (j=0; j<nf->nin; j++) {
        if (TYPE(nf->ain[j].type)==T_SYMBOL) {
            continue;
        }
        v = RARRAY_AREF(args,j);
        if (IsNArray(v)) {
            // set LARG(lp,j) with v
            GetNArray(v,na);
            nf_dim = nf->ain[j].dim;
            ndloop_check_shape(lp, nf_dim, na);
            dim_beg = lp->ndim + nf->ain[j].dim - na->ndim;
            for (i=0; i<na->ndim; i++) {
                dim_map[i] = lp->trans_map[i+dim_beg];
                //printf("dim_map[%d]=%d na->shape[%d]=%d\n",i,dim_map[i],i,na->shape[i]);
            }
            if (nf->ain[j].type==OVERWRITE) {
                lp->xargs[j].flag = flag = NDL_WRITE;
            } else {
                lp->xargs[j].flag = flag = NDL_READ;
            }
            ndloop_set_stepidx(lp, j, v, dim_map, flag);
            LARG(lp,j).ndim = nf_dim;
            if (nf_dim > 0) {
                LARG(lp,j).shape = na->shape + (na->ndim - nf_dim);
            }
        } else if (TYPE(v)==T_ARRAY) {
            LARG(lp,j).value = v;
            LARG(lp,j).elmsz = sizeof(VALUE);
            LARG(lp,j).ptr   = NULL;
            for (i=0; i<=max_nd; i++) {
                LITER(lp,i,j).step = 1;
            }
        }
    }
    // check whether # of element is zero
    for (s=1,i=0; i<=max_nd; i++) {
        s *= lp->n[i];
    }
    if (s==0) {
        for (i=0; i<=max_nd; i++) {
            lp->n[i] = 0;
        }
    }
}


static int
ndloop_check_inplace(VALUE type, int na_ndim, size_t *na_shape, VALUE v)
{
    int i;
    narray_t *na;

    // type check
    if (type != CLASS_OF(v)) {
        return 0;
    }
    GetNArray(v,na);
    // shape check
    if (na->ndim != na_ndim) {
        return 0;
    }
    for (i=0; i<na_ndim; i++) {
        if (na_shape[i] != na->shape[i]) {
            return 0;
        }
    }
    // v is selected as output
    return 1;
}

static VALUE
ndloop_find_inplace(ndfunc_t *nf, na_md_loop_t *lp, VALUE type,
                    int na_ndim, size_t *na_shape, VALUE args)
{
    int j;
    VALUE v;

    // find inplace
    for (j=0; j<nf->nin; j++) {
        v = RARRAY_AREF(args,j);
        if (IsNArray(v)) {
            if (TEST_INPLACE(v)) {
                if (ndloop_check_inplace(type,na_ndim,na_shape,v)) {
                    // if already copied, create outary and write-back
                    if (lp->copy_flag & (1<<j)) {
                        lp->writeback = j;
                    }
                    return v;
                }
            }
        }
    }
    // find casted or copied input array
    for (j=0; j<nf->nin; j++) {
        if (lp->copy_flag & (1<<j)) {
            v = RARRAY_AREF(args,j);
            if (ndloop_check_inplace(type,na_ndim,na_shape,v)) {
                return v;
            }
        }
    }
    return Qnil;
}



static VALUE
ndloop_get_arg_type(ndfunc_t *nf, VALUE args, VALUE t)
{
    int i;

    // if type is FIXNUM, get the type of i-th argument
    if (FIXNUM_P(t)) {
        i = FIX2INT(t);
        if (i<0 || i>=nf->nin) {
            rb_bug("invalid type: index (%d) out of # of args",i);
        }
        t = nf->ain[i].type;
        // if i-th type is Qnil, get the type of i-th input value
        if (!CASTABLE(t)) {
            t = CLASS_OF(RARRAY_AREF(args,i));
        }
    }
    return t;
}


static VALUE
ndloop_set_output_narray(ndfunc_t *nf, na_md_loop_t *lp, int k,
                         VALUE type, VALUE args)
{
    int i, j;
    int na_ndim;
    int lp_dim;
    volatile VALUE v=Qnil;
    size_t *na_shape;
    int *dim_map;
    int flag = NDL_READ_WRITE;
    int nd;
    int max_nd = lp->ndim + nf->aout[k].dim;

    na_shape = ALLOCA_N(size_t, max_nd);
    dim_map = ALLOCA_N(int, max_nd);

    //printf("max_nd=%d lp->ndim=%d\n",max_nd,lp->ndim);

    // md-loop shape
    na_ndim = 0;
    for (i=0; i<lp->ndim; i++) {
        // na_shape[i] == lp->n[lp->trans_map[i]]
        lp_dim = lp->trans_map[i];
        //printf("i=%d lp_dim=%d\n",i,lp_dim);
        if (NDF_TEST(nf,NDF_CUM)) {   // cumulate with shape kept
            na_shape[na_ndim] = lp->n[lp_dim];
        } else
        if (na_test_reduce(lp->reduce,lp_dim)) {   // accumulate dimension
            if (NDF_TEST(nf,NDF_KEEP_DIM)) {
                na_shape[na_ndim] = 1;         // leave it
            } else {
                continue;  // delete dimension
            }
        } else {
            na_shape[na_ndim] = lp->n[lp_dim];
        }
        //printf("i=%d lp_dim=%d na_shape[%d]=%ld\n",i,lp_dim,i,na_shape[i]);
        dim_map[na_ndim++] = lp_dim;
        //dim_map[lp_dim] = na_ndim++;
    }

    // user-specified shape
    for (i=0; i<nf->aout[k].dim; i++) {
        na_shape[na_ndim] = nf->aout[k].shape[i];
        dim_map[na_ndim++] = i + lp->ndim;
    }

    // find inplace from input arrays
    if (k==0 && NDF_TEST(nf,NDF_INPLACE)) {
        v = ndloop_find_inplace(nf,lp,type,na_ndim,na_shape,args);
    }
    if (!RTEST(v)) {
        // new object
        v = rb_narray_new(type, na_ndim, na_shape);
        flag = NDL_WRITE;
    }

    j = lp->nin + k;
    ndloop_set_stepidx(lp, j, v, dim_map, flag);
    LARG(lp,j).ndim = nd = nf->aout[k].dim;
    if (nd > 0) {
        LARG(lp,j).shape = nf->aout[k].shape;
    }

    return v;
}

static VALUE
ndloop_set_output(ndfunc_t *nf, na_md_loop_t *lp, VALUE args)
{
    int i, j, k, idx;
    volatile VALUE v, t, results;
    VALUE init;

    int max_nd = lp->ndim + lp->user.ndim;

    // output results
    results = rb_ary_new2(nf->nout);

    for (k=0; k<nf->nout; k++) {
        t = nf->aout[k].type;
        t = ndloop_get_arg_type(nf,args,t);

        if (rb_obj_is_kind_of(t, rb_cClass)) {
            if (RTEST(rb_class_inherited_p(t, cNArray))) {
                // NArray
                v = ndloop_set_output_narray(nf,lp,k,t,args);
                rb_ary_push(results, v);
            }
            else if (RTEST(rb_class_inherited_p(t, rb_cArray))) {
                // Ruby Array
                j = lp->nin + k;
                for (i=0; i<=max_nd; i++) {
                    LITER(lp,i,j).step = sizeof(VALUE);
                }
                LARG(lp,j).value = t;
                LARG(lp,j).elmsz = sizeof(VALUE);
            } else {
                rb_raise(rb_eRuntimeError,"ndloop_set_output: invalid for type");
            }
        }
    }

    // initialilzer
    k = lp->init_aidx;
    if (k > -1) {
        idx = nf->ain[k].dim;
        v = RARRAY_AREF(results,idx);
        init = RARRAY_AREF(args,k);
        na_store(v,init);
    }

    return results;
}


static void
ndfunc_contract_loop(na_md_loop_t *lp)
{
    int i,j,k,success,cnt=0;
    int red0, redi;

    redi = na_test_reduce(lp->reduce,0);

    //for (i=0; i<lp->ndim; i++) {
    //    printf("lp->n[%d]=%lu\n",i,lp->n[i]);
    //}

    for (i=1; i<lp->ndim; i++) {
        red0 = redi;
        redi = na_test_reduce(lp->reduce,i);
        //printf("contract i=%d reduce_cond=%d %d\n",i,red0,redi);
        if (red0 != redi) {
            continue;
        }
        success = 1;
        for (j=0; j<lp->narg; j++) {
            if (!(LITER(lp,i,j).idx == NULL &&
                  LITER(lp,i-1,j).idx == NULL &&
                  LITER(lp,i-1,j).step == LITER(lp,i,j).step*(ssize_t)(lp->n[i]))) {
                success = 0;
                break;
            }
        }
        if (success) {
            //printf("contract i=%d-th and %d-th\n",i-1,i);
            // contract (i-1)-th and i-th dimension
            lp->n[i] *= lp->n[i-1];
            // shift dimensions
            for (k=i-1; k>cnt; k--) {
                lp->n[k] = lp->n[k-1];
            }
            //printf("k=%d\n",k);
            for (; k>=0; k--) {
                lp->n[k] = 1;
            }
            for (j=0; j<lp->narg; j++) {
                for (k=i-1; k>cnt; k--) {
                    LITER(lp,k,j) = LITER(lp,k-1,j);
                }
            }
            if (redi) {
                lp->reduce_dim--;
            }
            cnt++;
        }
    }
    //printf("contract cnt=%d\n",cnt);
    if (cnt>0) {
        for (j=0; j<lp->narg; j++) {
            lp->xargs[j].iter = &LITER(lp,cnt,j);
        }
        lp->n = &(lp->n[cnt]);
        lp->ndim -= cnt;
        //for (i=0; i<lp->ndim; i++) {printf("lp->n[%d]=%lu\n",i,lp->n[i]);}
    }
}


static void
ndfunc_set_user_loop(ndfunc_t *nf, na_md_loop_t *lp)
{
    int j, ud=0;

    if (lp->reduce_dim > 0) {
        ud = lp->reduce_dim;
    }
    else if (lp->ndim > 0 && NDF_TEST(nf,NDF_HAS_LOOP)) {
        ud = 1;
    }
    else {
        goto skip_ud;
    }
    if (ud > lp->ndim) {
        rb_bug("Reduce-dimension is larger than loop-dimension");
    }
    // increase user dimension
    lp->user.ndim += ud;
    lp->ndim -= ud;
    for (j=0; j<lp->narg; j++) {
        if (LARG(lp,j).shape) {
            rb_bug("HAS_LOOP or reduce-dimension=%d conflicts with user-dimension",lp->reduce_dim);
        }
        LARG(lp,j).ndim += ud;
        LARG(lp,j).shape = &(lp->n[lp->ndim]);
        //printf("LARG(lp,j).ndim=%d,LARG(lp,j).shape=%lx\n",LARG(lp,j).ndim,(size_t)LARG(lp,j).shape);
    }
    //printf("lp->reduce_dim=%d lp->user.ndim=%d lp->ndim=%d\n",lp->reduce_dim,lp->user.ndim,lp->ndim);

 skip_ud:
    lp->user.n = &(lp->n[lp->ndim]);
    for (j=0; j<lp->narg; j++) {
        LARG(lp,j).iter = &LITER(lp,lp->ndim,j);
        //printf("in ndfunc_set_user_loop: lp->user.args[%d].iter=%lx\n",j,(size_t)(LARG(lp,j).iter));
    }
}


static void
ndfunc_set_bufcp(na_md_loop_t *lp, unsigned int loop_spec)
{
    unsigned int f;
    int i, j;
    int nd, ndim;
    boolean zero_step;
    ssize_t n, sz, elmsz, stride, n_total, last_step;
    size_t *buf_shape;
    na_loop_iter_t *buf_iter=NULL, *src_iter;

    //if (loop_spec==0) return;

    n_total = lp->user.n[0];
    for (i=1; i<lp->user.ndim; i++) {
        n_total *= lp->user.n[i];
    }

    //for (j=0; j<lp->nin; j++) {
    for (j=0; j<lp->narg; j++) {
        //ndim = nd = lp->user.ndim;
        ndim = nd = LARG(lp,j).ndim;
        sz = elmsz = LARG(lp,j).elmsz;
        src_iter = LARG(lp,j).iter;
        last_step = src_iter[ndim-1].step;
        f = 0;
        zero_step = 1;
        for (i=ndim; i>0; ) {
            i--;
            if (LARG(lp,j).shape) {
                n = LARG(lp,j).shape[i];
            } else {
                printf("shape is NULL\n");
                n = lp->user.n[i];
            }
            stride = sz * n;
            //printf("{j=%d,i=%d,ndim=%d,nd=%d,idx=%lx,step=%ld,n=%ld,sz=%ld,stride=%ld}\n",j,i,ndim,nd,(size_t)src_iter[i].idx,src_iter[i].step,n,sz,stride);
            if (src_iter[i].idx) {
                f |= 2;  // INDEX LOOP
                zero_step = 0;
            } else {
                if (src_iter[i].step != sz) {
                    f |= 1;  // NON_CONTIGUOUS LOOP
                } else {
                    // CONTIGUOUS LOOP
                    if (i==ndim-1) {  // contract if last dimension
                        ndim = i;
                        elmsz = stride;
                    }
                }
                if (src_iter[i].step != 0) {
                    zero_step = 0;
                }
            }
            sz = stride;
        }
        //printf("[j=%d f=%d loop_spec=%d zero_step=%d]\n",j,f,loop_spec,zero_step);

        if (zero_step) {
            // no buffer needed
            continue;
        }

        // should check flatten-able loop to avoid buffering


        // over loop_spec or reduce_loop is not contiguous
        if (f & loop_spec || (lp->reduce_dim > 1 && ndim > 0)) {
            //printf("(buf,nd=%d)",nd);
            buf_iter = ALLOC_N(na_loop_iter_t,nd+3);
            buf_shape = ALLOC_N(size_t,nd);
            buf_iter[nd].pos = 0;
            buf_iter[nd].step = 0;
            buf_iter[nd].idx = NULL;
            sz = LARG(lp,j).elmsz;
            last_step = sz;
            for (i=nd; i>0; ) {
                i--;
                buf_iter[i].pos = 0;
                buf_iter[i].step = sz;
                buf_iter[i].idx = NULL;
                //n = lp->user.n[i];
                n = LARG(lp,j).shape[i];
                buf_shape[i] = n;
                sz *= n;
            }
            LBUFCP(lp,j) = ALLOC(na_buffer_copy_t);
            LBUFCP(lp,j)->ndim = ndim;
            LBUFCP(lp,j)->elmsz = elmsz;
            LBUFCP(lp,j)->n = buf_shape;
            LBUFCP(lp,j)->src_iter = src_iter;
            LBUFCP(lp,j)->buf_iter = buf_iter;
            LARG(lp,j).iter = buf_iter;
            //printf("in ndfunc_set_bufcp(1): lp->user.args[%d].iter=%lx\n",j,(size_t)(LARG(lp,j).iter));
            LBUFCP(lp,j)->src_ptr = LARG(lp,j).ptr;
            LARG(lp,j).ptr = LBUFCP(lp,j)->buf_ptr = xmalloc(sz);
            //printf("(LBUFCP(lp,%d)->buf_ptr=%lx)\n",j,(size_t)(LBUFCP(lp,j)->buf_ptr));
        }
    }

    // flatten reduce dimensions
    for (j=0; j<lp->narg; j++) {
        ndim = lp->user.ndim;
        src_iter = LARG(lp,j).iter;
        last_step = src_iter[ndim-1].step;
        if (lp->reduce_dim>1) {
            //printf("(reduce_dim=%d,ndim=%d,nd=%d,n=%ld,lst=%ld)\n",lp->reduce_dim,ndim,nd,n_total,last_step);
            buf_iter = ALLOC_N(na_loop_iter_t,2);
            buf_iter[0].pos = LARG(lp,j).iter[0].pos;
            buf_iter[0].step = last_step;
            buf_iter[0].idx = NULL;
            buf_iter[1].pos = 0;
            buf_iter[1].step = 0;
            buf_iter[1].idx = NULL;
            LARG(lp,j).iter = buf_iter;
            //printf("in ndfunc_set_bufcp(2): lp->user.args[%d].iter=%lx\n",j,(size_t)(LARG(lp,j).iter));
            lp->xargs[j].free_user_iter = 1;
        }
    }

    // flatten reduce dimensions
    if (lp->reduce_dim>1) {
        lp->user.n[0] = n_total;
        lp->user.ndim = 1;
    }
}


static void
ndloop_copy_to_buffer(na_buffer_copy_t *lp)
{
    size_t *c;
    char *src, *buf;
    int  i;
    int  nd = lp->ndim;
    size_t elmsz = lp->elmsz;
    size_t buf_pos = 0;
    DBG(size_t j);

    //printf("\nto_buf nd=%d elmsz=%ld\n",nd,elmsz);
    DBG(printf("<to buf> ["));
    // zero-dimension
    if (nd==0) {
        src = lp->src_ptr + LITER_SRC(lp,0).pos;
        buf = lp->buf_ptr;
        memcpy(buf,src,elmsz);
        DBG(for (j=0; j<elmsz/8; j++) {printf("%g,",((double*)(buf))[j]);});
        goto loop_end;
    }
    // initialize loop counter
    c = ALLOCA_N(size_t, nd+1);
    for (i=0; i<=nd; i++) c[i]=0;
    // loop body
    for (i=0;;) {
        // i-th dimension
        for (; i<nd; i++) {
            if (LITER_SRC(lp,i).idx) {
                LITER_SRC(lp,i+1).pos = LITER_SRC(lp,i).pos + LITER_SRC(lp,i).idx[c[i]];
            } else {
                LITER_SRC(lp,i+1).pos = LITER_SRC(lp,i).pos + LITER_SRC(lp,i).step*c[i];
            }
        }
        src = lp->src_ptr + LITER_SRC(lp,nd).pos;
        buf = lp->buf_ptr + buf_pos;
        memcpy(buf,src,elmsz);
        DBG(for (j=0; j<elmsz/8; j++) {printf("%g,",((double*)(buf))[j]);});
        buf_pos += elmsz;
        // count up
        for (;;) {
            if (i<=0) goto loop_end;
            i--;
            if (++c[i] < lp->n[i]) break;
            c[i] = 0;
        }
    }
 loop_end:
    ;
    DBG(printf("]\n"));
}

static void
ndloop_copy_from_buffer(na_buffer_copy_t *lp)
{
    size_t *c;
    char *src, *buf;
    int  i;
    int  nd = lp->ndim;
    size_t elmsz = lp->elmsz;
    size_t buf_pos = 0;
    DBG(size_t j);

    //printf("\nfrom_buf nd=%d elmsz=%ld\n",nd,elmsz);
    DBG(printf("<from buf> ["));
    // zero-dimension
    if (nd==0) {
        src = lp->src_ptr + LITER_SRC(lp,0).pos;
        buf = lp->buf_ptr;
        memcpy(src,buf,elmsz);
        DBG(for (j=0; j<elmsz/8; j++) {printf("%g,",((double*)(src))[j]);});
        goto loop_end;
    }
    // initialize loop counter
    c = ALLOCA_N(size_t, nd+1);
    for (i=0; i<=nd; i++) c[i]=0;
    // loop body
    for (i=0;;) {
        // i-th dimension
        for (; i<nd; i++) {
            if (LITER_SRC(lp,i).idx) {
                LITER_SRC(lp,i+1).pos = LITER_SRC(lp,i).pos + LITER_SRC(lp,i).idx[c[i]];
            } else {
                LITER_SRC(lp,i+1).pos = LITER_SRC(lp,i).pos + LITER_SRC(lp,i).step*c[i];
            }
        }
        src = lp->src_ptr + LITER_SRC(lp,nd).pos;
        buf = lp->buf_ptr + buf_pos;
        memcpy(src,buf,elmsz);
        DBG(for (j=0; j<elmsz/8; j++) {printf("%g,",((double*)(src))[j]);});
        buf_pos += elmsz;
        // count up
        for (;;) {
            if (i<=0) goto loop_end;
            i--;
            if (++c[i] < lp->n[i]) break;
            c[i] = 0;
        }
    }
 loop_end:
    DBG(printf("]\n"));
}


static void
ndfunc_write_back(ndfunc_t *nf, na_md_loop_t *lp, VALUE orig_args, VALUE results)
{
    VALUE src, dst;

    if (lp->writeback >= 0) {
        dst = RARRAY_AREF(orig_args,lp->writeback);
        src = RARRAY_AREF(results,0);
        na_store(dst,src);
        RARRAY_ASET(results,0,dst);
    }
}


static VALUE
ndloop_extract(VALUE results, ndfunc_t *nf)
{
    static ID id_extract = 0;
    long n, i;
    VALUE x, y;
    narray_t *na;

    if (id_extract==0) {
        id_extract = rb_intern("extract");
    }

    // extract result objects
    switch(nf->nout) {
    case 0:
        return Qnil;
    case 1:
        x = RARRAY_AREF(results,0);
        if (NDF_TEST(nf,NDF_EXTRACT)) {
            if (IsNArray(x)){
                GetNArray(x,na);
                if (NA_NDIM(na)==0) {
                    x = rb_funcall(x, id_extract, 0);
                }
            }
        }
        return x;
    }
    if (NDF_TEST(nf,NDF_EXTRACT)) {
        n = RARRAY_LEN(results);
        for (i=0; i<n; i++) {
            x = RARRAY_AREF(results,i);
            if (IsNArray(x)){
                GetNArray(x,na);
                if (NA_NDIM(na)==0) {
                    y = rb_funcall(x, id_extract, 0);
                    RARRAY_ASET(results,i,y);
                }
            }
        }
    }
    return results;
}


static void
loop_narray(ndfunc_t *nf, na_md_loop_t *lp);

static VALUE
ndloop_run(VALUE vlp)
{
    unsigned int loop_spec;
    volatile VALUE args, orig_args, results;
    na_md_loop_t *lp = (na_md_loop_t*)(vlp);
    ndfunc_t *nf;

    orig_args = lp->vargs;
    nf = lp->ndfunc;

    args = rb_obj_dup(orig_args);

    // setup ndloop iterator with arguments
    ndloop_init_args(nf, lp, args);
    results = ndloop_set_output(nf, lp, args);

    if (na_debug_flag) {
        printf("-- ndloop_set_output --\n");
        print_ndloop(lp);
    }

    // contract loop
    if (lp->loop_func == loop_narray) {
        ndfunc_contract_loop(lp);
        if (na_debug_flag) {
            printf("-- ndfunc_contract_loop --\n");
            print_ndloop(lp);
        }
    }

    // setup objects in which resuts are stored
    ndfunc_set_user_loop(nf, lp);

    // setup buffering during loop
    if (lp->loop_func == loop_narray) {
        loop_spec = ndloop_func_loop_spec(nf, lp->user.ndim);
        ndfunc_set_bufcp(lp, loop_spec);
        if (na_debug_flag) {
            printf("-- ndfunc_set_bufcp --\n");
            print_ndloop(lp);
        }
    }

    // loop
    (*(lp->loop_func))(nf, lp);

    if (na_debug_flag) {
        printf("-- after loop --\n");
        print_ndloop(lp);
    }

    if (RTEST(lp->user.err_type)) {
        rb_raise(lp->user.err_type, "error in NArray operation");
    }

    // write-back will be placed here
    ndfunc_write_back(nf, lp, orig_args, results);

    // extract result objects
    return ndloop_extract(results, nf);
}


// ---------------------------------------------------------------------------

static void
loop_narray(ndfunc_t *nf, na_md_loop_t *lp)
{
    size_t *c;
    int  i, j;
    int  nd = lp->ndim;

    if (nd<0) {
        rb_bug("bug? lp->ndim = %d\n", lp->ndim);
    }

    if (nd==0) {
        for (j=0; j<lp->nin; j++) {
            if (lp->xargs[j].bufcp) {
                //printf("copy_to_buffer j=%d\n",j);
                ndloop_copy_to_buffer(lp->xargs[j].bufcp);
            }
        }
        (*(nf->func))(&(lp->user));
        for (j=0; j<lp->narg; j++) {
            if (lp->xargs[j].bufcp && (lp->xargs[j].flag & NDL_WRITE)) {
                //printf("copy_from_buffer j=%d\n",j);
                // copy data to work buffer
                ndloop_copy_from_buffer(lp->xargs[j].bufcp);
            }
        }
        return;
    }

    // initialize loop counter
    c = ALLOCA_N(size_t, nd+1);
    for (i=0; i<=nd; i++) c[i]=0;

    // loop body
    for (i=0;;) {
        // i-th dimension
        for (; i<nd; i++) {
            // j-th argument
            for (j=0; j<lp->narg; j++) {
                if (LITER(lp,i,j).idx) {
                    LITER(lp,i+1,j).pos = LITER(lp,i,j).pos + LITER(lp,i,j).idx[c[i]];
                } else {
                    LITER(lp,i+1,j).pos = LITER(lp,i,j).pos + LITER(lp,i,j).step*c[i];
                }
                //printf("j=%d c[i=%d]=%lu pos=%lu\n",j,i,c[i],LITER(lp,i+1,j).pos);
            }
        }
        for (j=0; j<lp->nin; j++) {
            if (lp->xargs[j].bufcp) {
                // copy data to work buffer
                // cp lp->iter[j][nd..*] to lp->user.args[j].iter[0..*]
                //printf("copy_to_buffer j=%d\n",j);
                ndloop_copy_to_buffer(lp->xargs[j].bufcp);
            }
        }
        (*(nf->func))(&(lp->user));
        for (j=0; j<lp->narg; j++) {
            if (lp->xargs[j].bufcp && (lp->xargs[j].flag & NDL_WRITE)) {
                // copy data to work buffer
                //printf("copy_from_buffer j=%d\n",j);
                ndloop_copy_from_buffer(lp->xargs[j].bufcp);
            }
        }
        if (RTEST(lp->user.err_type)) {return;}

        for (;;) {
            if (i<=0) goto loop_end;
            i--;
            if (++c[i] < lp->n[i]) break;
            c[i] = 0;
        }
    }
 loop_end:
    ;
}


VALUE
na_ndloop_main(ndfunc_t *nf, VALUE args, void *opt_ptr)
{
    unsigned int copy_flag;
    na_md_loop_t lp;

    if (na_debug_flag) print_ndfunc(nf);

    // cast arguments to NArray
    copy_flag = ndloop_cast_args(nf, args);

    // allocate ndloop struct
    ndloop_alloc(&lp, nf, args, opt_ptr, copy_flag, loop_narray);

    return rb_ensure(ndloop_run, (VALUE)&lp, ndloop_release, (VALUE)&lp);
}


VALUE
#ifdef HAVE_STDARG_PROTOTYPES
na_ndloop(ndfunc_t *nf, int argc, ...)
#else
na_ndloop(nf, argc, va_alist)
  ndfunc_t *nf;
  int argc;
  va_dcl
#endif
{
    va_list ar;

    int i;
    VALUE *argv;
    volatile VALUE args;

    argv = ALLOCA_N(VALUE,argc);

    va_init_list(ar, argc);
    for (i=0; i<argc; i++) {
        argv[i] = va_arg(ar, VALUE);
    }
    va_end(ar);

    args = rb_ary_new4(argc, argv);

    return na_ndloop_main(nf, args, NULL);
}


VALUE
na_ndloop2(ndfunc_t *nf, VALUE args)
{
    return na_ndloop_main(nf, args, NULL);
}

VALUE
#ifdef HAVE_STDARG_PROTOTYPES
na_ndloop3(ndfunc_t *nf, void *ptr, int argc, ...)
#else
na_ndloop3(nf, ptr, argc, va_alist)
  ndfunc_t *nf;
  void *ptr;
  int argc;
  va_dcl
#endif
{
    va_list ar;

    int i;
    VALUE *argv;
    volatile VALUE args;

    argv = ALLOCA_N(VALUE,argc);

    va_init_list(ar, argc);
    for (i=0; i<argc; i++) {
        argv[i] = va_arg(ar, VALUE);
    }
    va_end(ar);

    args = rb_ary_new4(argc, argv);

    return na_ndloop_main(nf, args, ptr);
}

VALUE
na_ndloop4(ndfunc_t *nf, void *ptr, VALUE args)
{
    return na_ndloop_main(nf, args, ptr);
}

//----------------------------------------------------------------------

VALUE
na_info_str(VALUE ary)
{
    int nd, i;
    char tmp[32];
    VALUE buf;
    narray_t *na;

    GetNArray(ary,na);
    nd = na->ndim;

    buf = rb_str_new2(rb_class2name(CLASS_OF(ary)));
    if (NA_TYPE(na) == NARRAY_VIEW_T) {
        rb_str_cat(buf,"(view)",6);
    }
    rb_str_cat(buf,"#shape=[",8);
    if (nd>0) {
        for (i=0;;) {
            sprintf(tmp,"%"SZF"u",na->shape[i]);
            rb_str_cat2(buf,tmp);
            if (++i==nd) break;
            rb_str_cat(buf,",",1);
        }
    }
    rb_str_cat(buf,"]",1);
    return buf;
}


//----------------------------------------------------------------------

static void
ndloop_inspect_get_width(int *ncol, int *nrow)
{
    VALUE cols, rows;

    cols = rb_ivar_get(cNArray,rb_intern("inspect_cols"));
    if (RTEST(cols)) {
        *ncol = NUM2INT(cols);
    } else {
        *ncol = 80;
    }

    rows = rb_ivar_get(cNArray,rb_intern("inspect_rows"));
    if (RTEST(rows)) {
        *nrow = NUM2INT(rows);
    } else {
        *nrow = 20;
    }
}

static void
loop_inspect(ndfunc_t *nf, na_md_loop_t *lp)
{
    int nd, i, ii;
    size_t *c;
    int col=0, row=0;
    long len;
    VALUE str;
    int ncol;
    int nrow;
    na_text_func_t func = (na_text_func_t)(nf->func);
    VALUE buf, opt;

    nd = lp->ndim;
    buf = lp->loop_opt;
    //opt = *(VALUE*)(lp->user.opt_ptr);
    opt = lp->user.option;

    for (i=0; i<nd; i++) {
        if (lp->n[i] == 0) {
            rb_str_cat(buf,"[]",2);
            return;
        }
    }

    rb_str_cat(buf,"\n",1);

    c = ALLOCA_N(size_t, nd+1);
    for (i=0; i<=nd; i++) c[i]=0;

    if (nd>0) {
        rb_str_cat(buf,"[",1);
    } else {
        rb_str_cat(buf,"",0);
    }

    ndloop_inspect_get_width(&ncol,&nrow);

    col = nd*2;
    for (i=0;;) {
        if (i<nd-1) {
            for (ii=0; ii<i; ii++) rb_str_cat(buf," ",1);
            for (; ii<nd-1; ii++) rb_str_cat(buf,"[",1);
        }
        for (; i<nd; i++) {
            if (LITER(lp,i,0).idx) {
                LITER(lp,i+1,0).pos = LITER(lp,i,0).pos + LITER(lp,i,0).idx[c[i]];
            } else {
                LITER(lp,i+1,0).pos = LITER(lp,i,0).pos + LITER(lp,i,0).step*c[i];
            }
        }
        str = (*func)(LARG(lp,0).ptr, LITER(lp,i,0).pos, opt);

        len = RSTRING_LEN(str) + 2;
        if (ncol>0 && col+len > ncol-3) {
            rb_str_cat(buf,"...",3);
            c[i-1] = lp->n[i-1];
        } else {
            rb_str_append(buf, str);
            col += len;
        }
        for (;;) {
            if (i==0) goto loop_end;
            i--;
            if (++c[i] < lp->n[i]) break;
            rb_str_cat(buf,"]",1);
            c[i] = 0;
        }
        //line_break:
        rb_str_cat(buf,", ",2);
        if (i<nd-1) {
            rb_str_cat(buf,"\n ",2);
            col = nd*2;
            row++;
            if (row==nrow) {
                rb_str_cat(buf,"...",3);
                goto loop_end;
            }
        }
    }
    loop_end:
    ;
}


VALUE
na_ndloop_inspect(VALUE nary, na_text_func_t func, VALUE opt)
{
    volatile VALUE args;
    na_md_loop_t lp;
    VALUE buf;
    ndfunc_arg_in_t ain[3] = {{Qnil,0},{sym_loop_opt},{sym_option}};
    ndfunc_t nf = { (na_iter_func_t)func, NO_LOOP, 3, 0, ain, 0 };
    //nf = ndfunc_alloc(NULL, NO_LOOP, 1, 0, Qnil);

    buf = na_info_str(nary);

    if (na_get_pointer(nary)==NULL) {
        return rb_str_cat(buf,"(empty)",7);
    }

    //rb_p(args);
    //if (na_debug_flag) print_ndfunc(&nf);

    args = rb_ary_new3(3,nary,buf,opt);

    // cast arguments to NArray
    //ndloop_cast_args(nf, args);

    // allocate ndloop struct
    ndloop_alloc(&lp, &nf, args, NULL, 0, loop_inspect);

    rb_ensure(ndloop_run, (VALUE)&lp, ndloop_release, (VALUE)&lp);

    return buf;
}


//----------------------------------------------------------------------

static void
loop_rarray_to_narray(ndfunc_t *nf, na_md_loop_t *lp)
{
    size_t *c;
    int     i;
    VALUE  *a;
    int nd = lp->ndim;

    // counter
    c = ALLOCA_N(size_t, nd+1);
    for (i=0; i<=nd; i++) c[i]=0;

    // array at each dimension
    a = ALLOCA_N(VALUE, nd+1);
    a[0] = LARG(lp,0).value;

    // loop body
    for (i=0;;) {
        for (; i<nd; i++) {
            if (LITER(lp,i,1).idx) {
                LITER(lp,i+1,1).pos = LITER(lp,i,1).pos + LITER(lp,i,1).idx[c[i]];
            } else {
                LITER(lp,i+1,1).pos = LITER(lp,i,1).pos + LITER(lp,i,1).step*c[i];
            }
            //LITER(lp,i+1,0).pos = LITER(lp,i,0).pos + c[i];
            if (TYPE(a[i])==T_ARRAY) {
                if (c[i] < (size_t)RARRAY_LEN(a[i])) {
                    a[i+1] = RARRAY_AREF(a[i],c[i]);
                } else {
                    a[i+1] = Qnil;
                }
            } else { // not Array -- what about narray?
                if (c[i]==0) {
                    a[i+1] = a[i];
                } else {
                    a[i+1] = Qnil;
                }
            }
            //printf("c[i]=%d, i=%d\n",c[i],i);
        }

        //printf("a[i]=0x%x, i=%d\n",a[i],i);
        LARG(lp,0).value = a[i];

        (*(nf->func))(&(lp->user));

        for (;;) {
            if (i<=0) goto loop_end;
            i--; c[i]++;
            if (c[i] < lp->n[i]) break;
            c[i] = 0;
        }
    }
    loop_end:
    ;
}

VALUE
na_ndloop_cast_rarray_to_narray(ndfunc_t *nf, VALUE rary, VALUE nary)
{
    na_md_loop_t lp;
    VALUE args;

    //rb_p(args);
    if (na_debug_flag) print_ndfunc(nf);

    args = rb_assoc_new(rary,nary);

    // cast arguments to NArray
    //ndloop_cast_args(nf, args);

    // allocate ndloop struct
    ndloop_alloc(&lp, nf, args, NULL, 0, loop_rarray_to_narray);

    return rb_ensure(ndloop_run, (VALUE)&lp, ndloop_release, (VALUE)&lp);
}


VALUE
na_ndloop_cast_rarray_to_narray2(ndfunc_t *nf, VALUE rary, VALUE nary, VALUE opt)
{
    na_md_loop_t lp;
    VALUE args;

    //rb_p(args);
    if (na_debug_flag) print_ndfunc(nf);

    //args = rb_assoc_new(rary,nary);
    args = rb_ary_new3(3,rary,nary,opt);

    // cast arguments to NArray
    //ndloop_cast_args(nf, args);

    // allocate ndloop struct
    ndloop_alloc(&lp, nf, args, NULL, 0, loop_rarray_to_narray);

    return rb_ensure(ndloop_run, (VALUE)&lp, ndloop_release, (VALUE)&lp);
}


//----------------------------------------------------------------------

static void
loop_narray_to_rarray(ndfunc_t *nf, na_md_loop_t *lp)
{
    size_t *c;
    int i;
    //int nargs = nf->narg + nf->nres;
    int nd = lp->ndim;
    VALUE *a;
    volatile VALUE a0;

    // alloc counter
    c = ALLOCA_N(size_t, nd+1);
    for (i=0; i<=nd; i++) c[i]=0;
    //c[i]=1; // for zero-dim
    //fprintf(stderr,"in loop_narray_to_rarray, nd=%d\n",nd);

    a = ALLOCA_N(VALUE, nd+1);
    a[0] = a0 = lp->loop_opt;

    // loop body
    for (i=0;;) {
        for (; i<nd; i++) {
            if (LITER(lp,i,0).idx) {
                LITER(lp,i+1,0).pos = LITER(lp,i,0).pos + LITER(lp,i,0).idx[c[i]];
            } else {
                LITER(lp,i+1,0).pos = LITER(lp,i,0).pos + LITER(lp,i,0).step*c[i];
            }
            if (c[i]==0) {
                a[i+1] = rb_ary_new2(lp->n[i]);
                rb_ary_push(a[i],a[i+1]);
            }
        }

        //lp->user.info = a[i];
        LARG(lp,1).value = a[i];
        (*(nf->func))(&(lp->user));

        for (;;) {
            if (i<=0) goto loop_end;
            i--;
            if (++c[i] < lp->n[i]) break;
            c[i] = 0;
        }
    }
 loop_end:
    ;
}

VALUE
na_ndloop_cast_narray_to_rarray(ndfunc_t *nf, VALUE nary, VALUE fmt)
{
    na_md_loop_t lp;
    VALUE args, a0;

    //rb_p(args);
    if (na_debug_flag) print_ndfunc(nf);

    a0 = rb_ary_new();
    args = rb_ary_new3(3,nary,a0,fmt);

    // cast arguments to NArray
    //ndloop_cast_args(nf, args);

    // allocate ndloop struct
    ndloop_alloc(&lp, nf, args, NULL, 0, loop_narray_to_rarray);

    rb_ensure(ndloop_run, (VALUE)&lp, ndloop_release, (VALUE)&lp);
    return RARRAY_AREF(a0,0);
}
