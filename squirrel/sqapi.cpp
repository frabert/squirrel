/*
    see copyright notice in squirrel.h
*/
#include "sqpcheader.h"
#include "sqvm.h"
#include "sqstring.h"
#include "sqtable.h"
#include "sqarray.h"
#include "sqfuncproto.h"
#include "sqclosure.h"
#include "squserdata.h"
#include "sqcompiler.h"
#include "sqfuncstate.h"
#include "sqclass.h"

static bool sq_aux_gettypedarg(HSQUIRRELVM v,SQInteger idx,SQObjectType type,SQObjectPtr **o)
{
    *o = &stack_get(v,idx);
    if(sq_type(**o) != type){
        SQObjectPtr oval = v->PrintObjVal(**o);
        v->Raise_Error(_SC("wrong argument type, expected '%s' got '%.50s'"),IdType2Name(type),_stringval(oval));
        return false;
    }
    return true;
}

#define _GETSAFE_OBJ(v,idx,type,o) { if(!sq_aux_gettypedarg(v,idx,type,&o)) return SQ_ERROR; }

#define sq_aux_paramscheck(v,count) \
{ \
    if(sq_gettop(v) < count){ v->Raise_Error(_SC("not enough params in the stack")); return SQ_ERROR; }\
}


SQInteger sq_aux_invalidtype(HSQUIRRELVM v,SQObjectType type)
{
    SQUnsignedInteger buf_size = 100 *sizeof(SQChar);
    scsprintf(_ss(v)->GetScratchPad(buf_size), buf_size, _SC("unexpected type %s"), IdType2Name(type));
    return sq_throwerror(v, _ss(v)->GetScratchPad(-1));
}

HSQUIRRELVM sq_open(SQInteger initialstacksize)
{
    SQSharedState *ss;
    SQVM *v;
    sq_new(ss, SQSharedState);
    ss->Init();
    v = (SQVM *)SQ_MALLOC(sizeof(SQVM));
    new (v) SQVM(ss);
    ss->_root_vm = v;
    if(v->Init(NULL, initialstacksize)) {
        return v;
    } else {
        sq_delete(v, SQVM);
        return NULL;
    }
    return v;
}

HSQUIRRELVM sq_newthread(HSQUIRRELVM friendvm, SQInteger initialstacksize)
{
    SQSharedState *ss;
    SQVM *v;
    ss=_ss(friendvm);

    v= (SQVM *)SQ_MALLOC(sizeof(SQVM));
    new (v) SQVM(ss);

    if(v->Init(friendvm, initialstacksize)) {
        friendvm->Push(v);
        return v;
    } else {
        sq_delete(v, SQVM);
        return NULL;
    }
}

SQInteger sq_getvmstate(HSQUIRRELVM v)
{
    if(v->_suspended)
        return SQ_VMSTATE_SUSPENDED;
    else {
        if(v->_callsstacksize != 0) return SQ_VMSTATE_RUNNING;
        else return SQ_VMSTATE_IDLE;
    }
}

void sq_seterrorhandler(HSQUIRRELVM v)
{
    SQObject o = stack_get(v, -1);
    if(sq_isclosure(o) || sq_isnativeclosure(o) || sq_isnull(o)) {
        v->_errorhandler = o;
        v->Pop();
    }
}

void sq_setnativedebughook(HSQUIRRELVM v,SQDEBUGHOOK hook)
{
    v->_debughook_native = hook;
    v->_debughook_closure.Null();
    v->_debughook = hook?true:false;
}

void sq_setdebughook(HSQUIRRELVM v)
{
    SQObject o = stack_get(v,-1);
    if(sq_isclosure(o) || sq_isnativeclosure(o) || sq_isnull(o)) {
        v->_debughook_closure = o;
        v->_debughook_native = NULL;
        v->_debughook = !sq_isnull(o);
        v->Pop();
    }
}

void sq_close(HSQUIRRELVM v)
{
    SQSharedState *ss = _ss(v);
    _thread(ss->_root_vm)->Finalize();
    sq_delete(ss, SQSharedState);
}

SQInteger sq_getversion()
{
    return SQUIRREL_VERSION_NUMBER;
}

SQRESULT sq_compile(HSQUIRRELVM v,SQLEXREADFUNC read,SQUserPointer p,const SQChar *sourcename,SQBool raiseerror)
{
    SQObjectPtr o;
#ifndef NO_COMPILER
    if(Compile(v, read, p, sourcename, o, raiseerror?true:false, _ss(v)->_debuginfo)) {
        v->Push(SQClosure::Create(_ss(v), _funcproto(o), _table(v->_roottable)->GetWeakRef(OT_TABLE)));
        return SQ_OK;
    }
    return SQ_ERROR;
#else
    return sq_throwerror(v,_SC("this is a no compiler build"));
#endif
}

void sq_enabledebuginfo(HSQUIRRELVM v, SQBool enable)
{
    _ss(v)->_debuginfo = enable?true:false;
}

void sq_notifyallexceptions(HSQUIRRELVM v, SQBool enable)
{
    _ss(v)->_notifyallexceptions = enable?true:false;
}

void sq_addref(HSQUIRRELVM v,HSQOBJECT *po)
{
    if(!ISREFCOUNTED(sq_type(*po))) return;
#ifdef NO_GARBAGE_COLLECTOR
    __AddRef(po->_type,po->_unVal);
#else
    _ss(v)->_refs_table.AddRef(*po);
#endif
}

SQUnsignedInteger sq_getrefcount(HSQUIRRELVM v,HSQOBJECT *po)
{
    if(!ISREFCOUNTED(sq_type(*po))) return 0;
#ifdef NO_GARBAGE_COLLECTOR
   return po->_unVal.pRefCounted->_uiRef;
#else
   return _ss(v)->_refs_table.GetRefCount(*po);
#endif
}

SQBool sq_release(HSQUIRRELVM v,HSQOBJECT *po)
{
    if(!ISREFCOUNTED(sq_type(*po))) return SQTrue;
#ifdef NO_GARBAGE_COLLECTOR
    bool ret = (po->_unVal.pRefCounted->_uiRef <= 1) ? SQTrue : SQFalse;
    __Release(po->_type,po->_unVal);
    return ret; //the ret val doesn't work(and cannot be fixed)
#else
    return _ss(v)->_refs_table.Release(*po);
#endif
}

SQUnsignedInteger sq_getvmrefcount(HSQUIRRELVM SQ_UNUSED_ARG(v), const HSQOBJECT *po)
{
    if (!ISREFCOUNTED(sq_type(*po))) return 0;
    return po->_unVal.pRefCounted->_uiRef;
}

const SQChar *sq_objtostring(const HSQOBJECT *o)
{
    if(sq_type(*o) == OT_STRING) {
        return _stringval(*o);
    }
    return NULL;
}

SQInteger sq_objtointeger(const HSQOBJECT *o)
{
    if(sq_isnumeric(*o)) {
        return tointeger(*o);
    }
    return 0;
}

SQFloat sq_objtofloat(const HSQOBJECT *o)
{
    if(sq_isnumeric(*o)) {
        return tofloat(*o);
    }
    return 0;
}

SQBool sq_objtobool(const HSQOBJECT *o)
{
    if(sq_isbool(*o)) {
        return _integer(*o);
    }
    return SQFalse;
}

SQUserPointer sq_objtouserpointer(const HSQOBJECT *o)
{
    if(sq_isuserpointer(*o)) {
        return _userpointer(*o);
    }
    return 0;
}

void sq_pushnull(HSQUIRRELVM v)
{
    v->PushNull();
}

void sq_objnewnull(HSQUIRRELVM v,HSQOBJECT *obj)
{
    *obj = SQObjectPtr();
}

void sq_pushstring(HSQUIRRELVM v,const SQChar *s,SQInteger len)
{
    if(s)
        v->Push(SQObjectPtr(SQString::Create(_ss(v), s, len)));
    else v->PushNull();
}

void sq_objnewstring(HSQUIRRELVM v,const SQChar *s,SQInteger len,HSQOBJECT *obj)
{
    if(s)
        *obj = SQObjectPtr(SQString::Create(_ss(v), s, len));
    else *obj = SQObjectPtr();
}

void sq_pushinteger(HSQUIRRELVM v,SQInteger n)
{
    v->Push(n);
}

void sq_objnewinteger(HSQUIRRELVM v,SQInteger n,HSQOBJECT *obj)
{
    *obj = SQObjectPtr(n);
}

void sq_pushbool(HSQUIRRELVM v,SQBool b)
{
    v->Push(b?true:false);
}

void sq_objnewbool(HSQUIRRELVM v,SQBool b,HSQOBJECT *obj)
{
    *obj = SQObjectPtr(b?true:false);
}

void sq_pushfloat(HSQUIRRELVM v,SQFloat n)
{
    v->Push(n);
}

void sq_objnewfloat(HSQUIRRELVM v,SQFloat f,HSQOBJECT *obj)
{
    *obj = SQObjectPtr(f);
}

void sq_pushuserpointer(HSQUIRRELVM v,SQUserPointer p)
{
    v->Push(p);
}

void sq_objnewuserpointer(HSQUIRRELVM v,SQUserPointer ptr,HSQOBJECT *obj)
{
    *obj = SQObjectPtr(ptr);
}

void sq_pushthread(HSQUIRRELVM v, HSQUIRRELVM thread)
{
    v->Push(thread);
}

void sq_objnewthread(HSQUIRRELVM v, HSQUIRRELVM thread, HSQOBJECT *obj)
{
    *obj = SQObjectPtr(thread);
}

SQUserPointer sq_newuserdata(HSQUIRRELVM v,SQUnsignedInteger size)
{
    HSQOBJECT obj;
    SQUserPointer ptr = sq_objnewuserdata(v,size,&obj);
    v->Push(obj);
    return ptr;
}

SQUserPointer sq_objnewuserdata(HSQUIRRELVM v,SQUnsignedInteger size,HSQOBJECT *obj)
{
    SQUserData *ud = SQUserData::Create(_ss(v), size + SQ_ALIGNMENT);
    *obj = SQObjectPtr(ud);
    return (SQUserPointer)sq_aligning(ud + 1);
}

void sq_newtable(HSQUIRRELVM v)
{
    v->Push(SQTable::Create(_ss(v), 0));
}

void sq_objnewtable(HSQUIRRELVM v,HSQOBJECT *obj)
{
    *obj = SQObjectPtr(SQTable::Create(_ss(v), 0));
}

void sq_newtableex(HSQUIRRELVM v,SQInteger initialcapacity)
{
    v->Push(SQTable::Create(_ss(v), initialcapacity));
}

void sq_objnewtableex(HSQUIRRELVM v,SQInteger initialcapacity,HSQOBJECT *obj)
{
    *obj = SQObjectPtr(SQTable::Create(_ss(v), initialcapacity));
}

void sq_newarray(HSQUIRRELVM v,SQInteger size)
{
    v->Push(SQArray::Create(_ss(v), size));
}

void sq_objnewarray(HSQUIRRELVM v,SQInteger size,HSQOBJECT *obj)
{
    *obj = SQObjectPtr(SQArray::Create(_ss(v), size));
}

static SQRESULT _sq_newclass(HSQUIRRELVM v,const SQObjectPtr& base,SQObjectPtr& obj)
{
    SQClass *baseclass = NULL;
    if(sq_type(base) != OT_CLASS)
        return sq_throwerror(v,_SC("invalid base type"));
    baseclass = _class(base);
    SQClass *newclass = SQClass::Create(_ss(v), baseclass);
    obj = newclass;
    return SQ_OK;
}

static SQRESULT _sq_newclass(HSQUIRRELVM v,SQObjectPtr& obj)
{
    SQClass *newclass = SQClass::Create(_ss(v), NULL);
    obj = newclass;
    return SQ_OK;
}

SQRESULT sq_newclass(HSQUIRRELVM v,SQBool hasbase)
{
    SQObjectPtr cls;
    if(hasbase) {
        SQObjectPtr &base = stack_get(v,-1);
        if(SQ_FAILED(_sq_newclass(v,base,cls))) return SQ_ERROR;
    }
    if(hasbase) v->Pop();
    v->Push(cls);
    return SQ_OK;
}


SQRESULT sq_objnewclass(HSQUIRRELVM v,HSQOBJECT* base,HSQOBJECT *obj)
{
    SQObjectPtr cls;
    SQRESULT res = base?_sq_newclass(v,*base,cls):_sq_newclass(v,cls);
    if(SQ_SUCCEEDED(res)) {
        *obj = cls;
    }
    return res;
}

static SQBool _sq_instanceof(HSQUIRRELVM v,const SQObjectPtr& inst,const SQObjectPtr& cl)
{
    if(sq_type(inst) != OT_INSTANCE || sq_type(cl) != OT_CLASS)
        return sq_throwerror(v,_SC("invalid param type"));
    return _instance(inst)->InstanceOf(_class(cl))?SQTrue:SQFalse;
}

SQBool sq_instanceof(HSQUIRRELVM v)
{
    SQObjectPtr &inst = stack_get(v,-1);
    SQObjectPtr &cl = stack_get(v,-2);
    return _sq_instanceof(v,inst,cl);
}

SQBool sq_objinstanceof(HSQUIRRELVM v,const HSQOBJECT *inst,const HSQOBJECT *cl)
{
    return _sq_instanceof(v,*inst,*cl);
}

static SQRESULT _sq_arrayappend(HSQUIRRELVM v,const SQObjectPtr &arr,const SQObjectPtr &obj)
{
    if(sq_isarray(arr)) {
        _array(arr)->Append(obj);
        return SQ_OK;
    }
    return SQ_ERROR;
}

SQRESULT sq_arrayappend(HSQUIRRELVM v,SQInteger idx)
{
    sq_aux_paramscheck(v,2);
    SQObjectPtr &arr = stack_get(v,idx);
    if(SQ_SUCCEEDED(_sq_arrayappend(v,arr,v->GetUp(-1)))) {
        v->Pop();
        return SQ_OK;
    }
    return SQ_ERROR;
}

SQRESULT sq_objarrayappend(HSQUIRRELVM v,const HSQOBJECT *arr,const HSQOBJECT *obj)
{
    return _sq_arrayappend(v,*arr,*obj);
}

static SQRESULT _sq_arraypop(HSQUIRRELVM v,const SQObjectPtr& arr,SQObjectPtr& popped)
{
    if(sq_isarray(arr)) {
        SQArray* a = _array(arr);
        if(a->Size() > 0) {
            popped = a->Top();
            a->Pop();
            return SQ_OK;
        }
        return sq_throwerror(v, _SC("empty array"));
    }
    return SQ_ERROR;
}

SQRESULT sq_arraypop(HSQUIRRELVM v,SQInteger idx,SQBool pushval)
{
    sq_aux_paramscheck(v, 1);
    SQObjectPtr arr = &stack_get(v,idx);
    SQObjectPtr popped;
    if(SQ_SUCCEEDED(_sq_arraypop(v,arr,popped))) {
        if(pushval) v->Push(popped);
        return SQ_OK;
    }
    return SQ_ERROR;
}

SQRESULT sq_objarraypop(HSQUIRRELVM v,const HSQOBJECT *arr,HSQOBJECT *popped)
{
    SQObjectPtr obj;
    SQRESULT res = _sq_arraypop(v,*arr,obj);
    *popped = obj;
    return res;
}

static SQRESULT _sq_arrayresize(HSQUIRRELVM v,const SQObjectPtr &arr,SQInteger newsize)
{
    if(sq_isarray(arr)) {
        if(newsize >= 0) {
            _array(arr)->Resize(newsize);
            return SQ_OK;
        }
        return sq_throwerror(v,_SC("negative size"));
    }
    return SQ_ERROR;
}

SQRESULT sq_arrayresize(HSQUIRRELVM v,SQInteger idx,SQInteger newsize)
{
    sq_aux_paramscheck(v,1);
    SQObjectPtr arr = stack_get(v,idx);
    return _sq_arrayresize(v,arr,newsize);
}

SQRESULT sq_objarrayresize(HSQUIRRELVM v,const HSQOBJECT *arr,SQInteger newsize)
{
    return _sq_arrayresize(v,*arr,newsize);
}

static SQRESULT _sq_arrayreverse(HSQUIRRELVM v,const SQObjectPtr& o)
{
    if(sq_isarray(o)) {
        SQArray *arr = _array(o);
        if(arr->Size() > 0) {
            SQObjectPtr t;
            SQInteger size = arr->Size();
            SQInteger n = size >> 1; size -= 1;
            for(SQInteger i = 0; i < n; i++) {
                t = arr->_values[i];
                arr->_values[i] = arr->_values[size-i];
                arr->_values[size-i] = t;
            }
            return SQ_OK;
        }
        return SQ_OK;
    }
    return SQ_ERROR;
}

SQRESULT sq_arrayreverse(HSQUIRRELVM v,SQInteger idx)
{
    sq_aux_paramscheck(v, 1);
    SQObjectPtr &o = stack_get(v,idx);
    return _sq_arrayreverse(v,o);
}

SQRESULT sq_objarrayreverse(HSQUIRRELVM v,const HSQOBJECT *o)
{
    return _sq_arrayreverse(v,*o);
}

static SQRESULT _sq_arrayremove(HSQUIRRELVM v,const SQObjectPtr& arr,SQInteger itemidx)
{
    if(sq_isarray(arr)) {
        return _array(arr)->Remove(itemidx) ? SQ_OK : sq_throwerror(v,_SC("index out of range"));
    }
    return SQ_ERROR;
}

SQRESULT sq_arrayremove(HSQUIRRELVM v,SQInteger idx,SQInteger itemidx)
{
    sq_aux_paramscheck(v, 1);
    SQObjectPtr arr = stack_get(v,idx);
    return _sq_arrayremove(v,arr,itemidx);
}

SQRESULT sq_objarrayremove(HSQUIRRELVM v,const HSQOBJECT *arr,SQInteger itemidx)
{
    return _sq_arrayremove(v,*arr,itemidx);
}

static SQRESULT _sq_arrayinsert(HSQUIRRELVM v,const SQObjectPtr& arr,SQInteger destpos,const SQObjectPtr& obj)
{
    if(sq_isarray(arr)) {
        SQRESULT ret = _array(arr)->Insert(destpos, obj) ? SQ_OK : sq_throwerror(v,_SC("index out of range"));
        return SQ_OK;
    }
    return SQ_ERROR;
}

SQRESULT sq_arrayinsert(HSQUIRRELVM v,SQInteger idx,SQInteger destpos)
{
    sq_aux_paramscheck(v, 1);
    SQObjectPtr &arr = stack_get(v,idx);
    if(SQ_SUCCEEDED(_sq_arrayinsert(v,arr,destpos,v->GetUp(-1)))) {
        v->Pop();
        return SQ_OK;
    }
    return SQ_ERROR;
}

SQRESULT sq_objarrayinsert(HSQUIRRELVM v,const HSQOBJECT *arr,SQInteger destpos,const HSQOBJECT *obj)
{
    return _sq_arrayinsert(v,*arr,destpos,*obj);
}

void sq_newclosure(HSQUIRRELVM v,SQFUNCTION func,SQUnsignedInteger nfreevars)
{
    SQNativeClosure *nc = SQNativeClosure::Create(_ss(v), func,nfreevars);
    nc->_nparamscheck = 0;
    for(SQUnsignedInteger i = 0; i < nfreevars; i++) {
        nc->_outervalues[i] = v->Top();
        v->Pop();
    }
    v->Push(SQObjectPtr(nc));
}

void sq_objnewclosure(HSQUIRRELVM v,SQFUNCTION func,HSQOBJECT *obj)
{
    SQNativeClosure *nc = SQNativeClosure::Create(_ss(v), func,0);
    nc->_nparamscheck = 0;
    *obj = SQObjectPtr(nc);
}

SQRESULT sq_getclosureinfo(HSQUIRRELVM v,SQInteger idx,SQInteger *nparams,SQInteger *nfreevars)
{
    SQObject o = stack_get(v, idx);
    if(sq_type(o) == OT_CLOSURE) {
        SQClosure *c = _closure(o);
        SQFunctionProto *proto = c->_function;
        *nparams = proto->_nparameters;
        *nfreevars = proto->_noutervalues;
        return SQ_OK;
    }
    else if(sq_type(o) == OT_NATIVECLOSURE)
    {
        SQNativeClosure *c = _nativeclosure(o);
        *nparams = c->_nparamscheck;
        *nfreevars = (SQInteger)c->_noutervalues;
        return SQ_OK;
    }
    return sq_throwerror(v,_SC("the object is not a closure"));
}

SQRESULT sq_setnativeclosurename(HSQUIRRELVM v,SQInteger idx,const SQChar *name)
{
    SQObject o = stack_get(v, idx);
    if(sq_isnativeclosure(o)) {
        SQNativeClosure *nc = _nativeclosure(o);
        nc->_name = SQString::Create(_ss(v),name);
        return SQ_OK;
    }
    return sq_throwerror(v,_SC("the object is not a nativeclosure"));
}

SQRESULT sq_setparamscheck(HSQUIRRELVM v,SQInteger nparamscheck,const SQChar *typemask)
{
    SQObject o = stack_get(v, -1);
    if(!sq_isnativeclosure(o))
        return sq_throwerror(v, _SC("native closure expected"));
    SQNativeClosure *nc = _nativeclosure(o);
    nc->_nparamscheck = nparamscheck;
    if(typemask) {
        SQIntVec res;
        if(!CompileTypemask(res, typemask))
            return sq_throwerror(v, _SC("invalid typemask"));
        nc->_typecheck.copy(res);
    }
    else {
        nc->_typecheck.resize(0);
    }
    if(nparamscheck == SQ_MATCHTYPEMASKSTRING) {
        nc->_nparamscheck = nc->_typecheck.size();
    }
    return SQ_OK;
}

SQRESULT sq_bindenv(HSQUIRRELVM v,SQInteger idx)
{
    SQObjectPtr &o = stack_get(v,idx);
    if(!sq_isnativeclosure(o) &&
        !sq_isclosure(o))
        return sq_throwerror(v,_SC("the target is not a closure"));
    SQObjectPtr &env = stack_get(v,-1);
    if(!sq_istable(env) &&
        !sq_isarray(env) &&
        !sq_isclass(env) &&
        !sq_isinstance(env))
        return sq_throwerror(v,_SC("invalid environment"));
    SQWeakRef *w = _refcounted(env)->GetWeakRef(sq_type(env));
    SQObjectPtr ret;
    if(sq_isclosure(o)) {
        SQClosure *c = _closure(o)->Clone();
        __ObjRelease(c->_env);
        c->_env = w;
        __ObjAddRef(c->_env);
        if(_closure(o)->_base) {
            c->_base = _closure(o)->_base;
            __ObjAddRef(c->_base);
        }
        ret = c;
    }
    else { //then must be a native closure
        SQNativeClosure *c = _nativeclosure(o)->Clone();
        __ObjRelease(c->_env);
        c->_env = w;
        __ObjAddRef(c->_env);
        ret = c;
    }
    v->Pop();
    v->Push(ret);
    return SQ_OK;
}

SQRESULT sq_getclosurename(HSQUIRRELVM v,SQInteger idx)
{
    SQObjectPtr &o = stack_get(v,idx);
    if(!sq_isnativeclosure(o) &&
        !sq_isclosure(o))
        return sq_throwerror(v,_SC("the target is not a closure"));
    if(sq_isnativeclosure(o))
    {
        v->Push(_nativeclosure(o)->_name);
    }
    else { //closure
        v->Push(_closure(o)->_function->_name);
    }
    return SQ_OK;
}

SQRESULT sq_setclosureroot(HSQUIRRELVM v,SQInteger idx)
{
    SQObjectPtr &c = stack_get(v,idx);
    SQObject o = stack_get(v, -1);
    if(!sq_isclosure(c)) return sq_throwerror(v, _SC("closure expected"));
    if(sq_istable(o)) {
        _closure(c)->SetRoot(_table(o)->GetWeakRef(OT_TABLE));
        v->Pop();
        return SQ_OK;
    }
    return sq_throwerror(v, _SC("invalid type"));
}

SQRESULT sq_getclosureroot(HSQUIRRELVM v,SQInteger idx)
{
    SQObjectPtr &c = stack_get(v,idx);
    if(!sq_isclosure(c)) return sq_throwerror(v, _SC("closure expected"));
    v->Push(_closure(c)->_root->_obj);
    return SQ_OK;
}

static SQRESULT _sq_clear(HSQUIRRELVM v,const SQObjectPtr& o)
{
    switch(sq_type(o)) {
        case OT_TABLE: _table(o)->Clear();  break;
        case OT_ARRAY: _array(o)->Resize(0); break;
        default:
            return sq_throwerror(v, _SC("clear only works on table and array"));
        break;

    }
    return SQ_OK;
}

SQRESULT sq_clear(HSQUIRRELVM v,SQInteger idx)
{
    SQObjectPtr o=stack_get(v,idx);
    return _sq_clear(v,o);
}

SQRESULT sq_objclear(HSQUIRRELVM v,const HSQOBJECT *o)
{
    return _sq_clear(v,*o);
}

void sq_pushroottable(HSQUIRRELVM v)
{
    v->Push(v->_roottable);
}

void sq_objgetroottable(HSQUIRRELVM v,HSQOBJECT *obj)
{
    *obj = v->_roottable;
}

void sq_pushregistrytable(HSQUIRRELVM v)
{
    v->Push(_ss(v)->_registry);
}

void sq_objgetregistrytable(HSQUIRRELVM v,HSQOBJECT *obj)
{
    *obj = _ss(v)->_registry;
}

void sq_pushconsttable(HSQUIRRELVM v)
{
    v->Push(_ss(v)->_consts);
}

void sq_objgetconsttable(HSQUIRRELVM v,HSQOBJECT *obj)
{
    *obj = _ss(v)->_consts;
}

SQRESULT sq_setroottable(HSQUIRRELVM v)
{
    SQObject o = stack_get(v, -1);
    if(sq_istable(o) || sq_isnull(o)) {
        v->_roottable = o;
        v->Pop();
        return SQ_OK;
    }
    return sq_throwerror(v, _SC("invalid type"));
}

SQRESULT sq_setconsttable(HSQUIRRELVM v)
{
    SQObject o = stack_get(v, -1);
    if(sq_istable(o)) {
        _ss(v)->_consts = o;
        v->Pop();
        return SQ_OK;
    }
    return sq_throwerror(v, _SC("invalid type, expected table"));
}

void sq_setforeignptr(HSQUIRRELVM v,SQUserPointer p)
{
    v->_foreignptr = p;
}

SQUserPointer sq_getforeignptr(HSQUIRRELVM v)
{
    return v->_foreignptr;
}

void sq_setsharedforeignptr(HSQUIRRELVM v,SQUserPointer p)
{
    _ss(v)->_foreignptr = p;
}

SQUserPointer sq_getsharedforeignptr(HSQUIRRELVM v)
{
    return _ss(v)->_foreignptr;
}

void sq_setvmreleasehook(HSQUIRRELVM v,SQRELEASEHOOK hook)
{
    v->_releasehook = hook;
}

SQRELEASEHOOK sq_getvmreleasehook(HSQUIRRELVM v)
{
    return v->_releasehook;
}

void sq_setsharedreleasehook(HSQUIRRELVM v,SQRELEASEHOOK hook)
{
    _ss(v)->_releasehook = hook;
}

SQRELEASEHOOK sq_getsharedreleasehook(HSQUIRRELVM v)
{
    return _ss(v)->_releasehook;
}

void sq_push(HSQUIRRELVM v,SQInteger idx)
{
    v->Push(stack_get(v, idx));
}

SQObjectType sq_gettype(HSQUIRRELVM v,SQInteger idx)
{
    return sq_type(stack_get(v, idx));
}

SQRESULT sq_typeof(HSQUIRRELVM v,SQInteger idx)
{
    SQObjectPtr &o = stack_get(v, idx);
    SQObjectPtr res;
    if(!v->TypeOf(o,res)) {
        return SQ_ERROR;
    }
    v->Push(res);
    return SQ_OK;
}

SQRESULT sq_tostring(HSQUIRRELVM v,SQInteger idx)
{
    SQObjectPtr &o = stack_get(v, idx);
    SQObjectPtr res;
    if(!v->ToString(o,res)) {
        return SQ_ERROR;
    }
    v->Push(res);
    return SQ_OK;
}

void sq_tobool(HSQUIRRELVM v, SQInteger idx, SQBool *b)
{
    SQObjectPtr &o = stack_get(v, idx);
    *b = SQVM::IsFalse(o)?SQFalse:SQTrue;
}

SQRESULT sq_getinteger(HSQUIRRELVM v,SQInteger idx,SQInteger *i)
{
    SQObjectPtr &o = stack_get(v, idx);
    if(sq_isnumeric(o)) {
        *i = tointeger(o);
        return SQ_OK;
    }
    if(sq_isbool(o)) {
        *i = SQVM::IsFalse(o)?SQFalse:SQTrue;
        return SQ_OK;
    }
    return SQ_ERROR;
}

SQRESULT sq_getfloat(HSQUIRRELVM v,SQInteger idx,SQFloat *f)
{
    SQObjectPtr &o = stack_get(v, idx);
    if(sq_isnumeric(o)) {
        *f = tofloat(o);
        return SQ_OK;
    }
    return SQ_ERROR;
}

SQRESULT sq_getbool(HSQUIRRELVM v,SQInteger idx,SQBool *b)
{
    SQObjectPtr &o = stack_get(v, idx);
    if(sq_isbool(o)) {
        *b = _integer(o);
        return SQ_OK;
    }
    return SQ_ERROR;
}

SQRESULT sq_getstringandsize(HSQUIRRELVM v,SQInteger idx,const SQChar **c,SQInteger *size)
{
    SQObjectPtr *o = NULL;
    _GETSAFE_OBJ(v, idx, OT_STRING,o);
    *c = _stringval(*o);
    *size = _string(*o)->_len;
    return SQ_OK;
}

SQRESULT sq_getstring(HSQUIRRELVM v,SQInteger idx,const SQChar **c)
{
    SQObjectPtr *o = NULL;
    _GETSAFE_OBJ(v, idx, OT_STRING,o);
    *c = _stringval(*o);
    return SQ_OK;
}

SQRESULT sq_getthread(HSQUIRRELVM v,SQInteger idx,HSQUIRRELVM *thread)
{
    SQObjectPtr *o = NULL;
    _GETSAFE_OBJ(v, idx, OT_THREAD,o);
    *thread = _thread(*o);
    return SQ_OK;
}

SQRESULT sq_clone(HSQUIRRELVM v,SQInteger idx)
{
    SQObjectPtr &o = stack_get(v,idx);
    v->PushNull();
    if(!v->Clone(o, stack_get(v, -1))){
        v->Pop();
        return SQ_ERROR;
    }
    return SQ_OK;
}

SQRESULT sq_objclone(HSQUIRRELVM v,const HSQOBJECT *o,HSQOBJECT *clone)
{
    SQObjectPtr dest;
    if(!v->Clone(*o, dest)){
        return SQ_ERROR;
    }
    *clone = dest;
    return SQ_OK;
}

static SQInteger _sq_getsize(HSQUIRRELVM v,const SQObjectPtr& o)
{
    SQObjectType type = sq_type(o);
    switch(type) {
    case OT_STRING:     return _string(o)->_len;
    case OT_TABLE:      return _table(o)->CountUsed();
    case OT_ARRAY:      return _array(o)->Size();
    case OT_USERDATA:   return _userdata(o)->_size;
    case OT_INSTANCE:   return _instance(o)->_class->_udsize;
    case OT_CLASS:      return _class(o)->_udsize;
    default:
        return sq_aux_invalidtype(v, type);
    }
}

SQInteger sq_getsize(HSQUIRRELVM v, SQInteger idx)
{
    SQObjectPtr &o = stack_get(v, idx);
    return _sq_getsize(v,o);
}

SQInteger sq_objgetsize(HSQUIRRELVM v,const HSQOBJECT *o)
{
    return _sq_getsize(v,*o);
}

SQHash sq_gethash(HSQUIRRELVM v, SQInteger idx)
{
    SQObjectPtr &o = stack_get(v, idx);
    return HashObj(o);
}

SQRESULT sq_getuserdata(HSQUIRRELVM v,SQInteger idx,SQUserPointer *p,SQUserPointer *typetag)
{
    SQObjectPtr *o = NULL;
    _GETSAFE_OBJ(v, idx, OT_USERDATA,o);
    (*p) = _userdataval(*o);
    if(typetag) *typetag = _userdata(*o)->_typetag;
    return SQ_OK;
}

SQRESULT sq_settypetag(HSQUIRRELVM v,SQInteger idx,SQUserPointer typetag)
{
    SQObjectPtr &o = stack_get(v,idx);
    switch(sq_type(o)) {
        case OT_USERDATA:   _userdata(o)->_typetag = typetag;   break;
        case OT_CLASS:      _class(o)->_typetag = typetag;      break;
        default:            return sq_throwerror(v,_SC("invalid object type"));
    }
    return SQ_OK;
}

SQRESULT sq_getobjtypetag(const HSQOBJECT *o,SQUserPointer * typetag)
{
  switch(sq_type(*o)) {
    case OT_INSTANCE: *typetag = _instance(*o)->_class->_typetag; break;
    case OT_USERDATA: *typetag = _userdata(*o)->_typetag; break;
    case OT_CLASS:    *typetag = _class(*o)->_typetag; break;
    default: return SQ_ERROR;
  }
  return SQ_OK;
}

SQRESULT sq_gettypetag(HSQUIRRELVM v,SQInteger idx,SQUserPointer *typetag)
{
    SQObjectPtr &o = stack_get(v,idx);
    if (SQ_FAILED(sq_getobjtypetag(&o, typetag)))
        return SQ_ERROR;// this is not an error it should be a bool but would break backward compatibility
    return SQ_OK;
}

SQRESULT sq_getuserpointer(HSQUIRRELVM v, SQInteger idx, SQUserPointer *p)
{
    SQObjectPtr *o = NULL;
    _GETSAFE_OBJ(v, idx, OT_USERPOINTER,o);
    (*p) = _userpointer(*o);
    return SQ_OK;
}

SQRESULT sq_setinstanceup(HSQUIRRELVM v, SQInteger idx, SQUserPointer p)
{
    SQObjectPtr &o = stack_get(v,idx);
    if(sq_type(o) != OT_INSTANCE) return sq_throwerror(v,_SC("the object is not a class instance"));
    _instance(o)->_userpointer = p;
    return SQ_OK;
}

SQRESULT sq_setclassudsize(HSQUIRRELVM v, SQInteger idx, SQInteger udsize)
{
    SQObjectPtr &o = stack_get(v,idx);
    if(sq_type(o) != OT_CLASS) return sq_throwerror(v,_SC("the object is not a class"));
    if(_class(o)->_locked) return sq_throwerror(v,_SC("the class is locked"));
    _class(o)->_udsize = udsize;
    return SQ_OK;
}


SQRESULT sq_getinstanceup(HSQUIRRELVM v, SQInteger idx, SQUserPointer *p,SQUserPointer typetag)
{
    SQObjectPtr &o = stack_get(v,idx);
    if(sq_type(o) != OT_INSTANCE) return sq_throwerror(v,_SC("the object is not a class instance"));
    (*p) = _instance(o)->_userpointer;
    if(typetag != 0) {
        SQClass *cl = _instance(o)->_class;
        do{
            if(cl->_typetag == typetag)
                return SQ_OK;
            cl = cl->_base;
        }while(cl != NULL);
        return sq_throwerror(v,_SC("invalid type tag"));
    }
    return SQ_OK;
}

SQInteger sq_gettop(HSQUIRRELVM v)
{
    return (v->_top) - v->_stackbase;
}

void sq_settop(HSQUIRRELVM v, SQInteger newtop)
{
    SQInteger top = sq_gettop(v);
    if(top > newtop)
        sq_pop(v, top - newtop);
    else
        while(top++ < newtop) sq_pushnull(v);
}

void sq_pop(HSQUIRRELVM v, SQInteger nelemstopop)
{
    assert(v->_top >= nelemstopop);
    v->Pop(nelemstopop);
}

void sq_poptop(HSQUIRRELVM v)
{
    assert(v->_top >= 1);
    v->Pop();
}


void sq_remove(HSQUIRRELVM v, SQInteger idx)
{
    v->Remove(idx);
}

SQInteger sq_cmp(HSQUIRRELVM v)
{
    SQInteger res;
    v->ObjCmp(stack_get(v, -1), stack_get(v, -2),res);
    return res;
}

static SQRESULT _sq_newslot(HSQUIRRELVM v,const SQObjectPtr& self,const SQObjectPtr& key,const SQObjectPtr& value,SQBool bstatic)
{
    if(sq_type(self) == OT_TABLE || sq_type(self) == OT_CLASS) {
        if(sq_type(key) == OT_NULL) return sq_throwerror(v, _SC("null is not a valid key"));
        v->NewSlot(self, key, value,bstatic?true:false);
    }
    return SQ_OK;
}

SQRESULT sq_newslot(HSQUIRRELVM v, SQInteger idx, SQBool bstatic)
{
    sq_aux_paramscheck(v, 3);
    SQObjectPtr &self = stack_get(v, idx);
    SQObjectPtr &key = v->GetUp(-2);
    SQObjectPtr &value = v->GetUp(-1);
    if(SQ_SUCCEEDED(_sq_newslot(v,self,key,value,bstatic))) {
        v->Pop(2);
        return SQ_OK;
    }
    return SQ_ERROR;
}

SQRESULT sq_objnewslot(HSQUIRRELVM v,const HSQOBJECT *self,const HSQOBJECT *key,const HSQOBJECT *value,SQBool bstatic)
{
    return _sq_newslot(v,*self,*key,*value,bstatic);
}

static SQRESULT _sq_deleteslot(HSQUIRRELVM v,const SQObjectPtr& self,const SQObjectPtr& key,SQObjectPtr& deleted)
{
    if(sq_istable(self)) {
        if(sq_type(key) == OT_NULL) return sq_throwerror(v, _SC("null is not a valid key"));
        if(!v->DeleteSlot(self, key, deleted)){
            return SQ_ERROR;
        }
        return SQ_OK;
    }
    return SQ_ERROR;
}

SQRESULT sq_deleteslot(HSQUIRRELVM v,SQInteger idx,SQBool pushval)
{
    sq_aux_paramscheck(v, 2);
    SQObjectPtr &self = stack_get(v,idx);
    SQObjectPtr &key = v->GetUp(-1);
    SQObjectPtr res;
    if(SQ_FAILED(_sq_deleteslot(v,self,key,res))) {
        v->Pop();
        return SQ_ERROR;
    }
    if(pushval) {
        v->GetUp(-1) = res;
    } else {
        v->Pop();
    }
    return SQ_OK;
}

SQRESULT sq_objdeleteslot(HSQUIRRELVM v,const HSQOBJECT *self,const HSQOBJECT *key,HSQOBJECT *deleted)
{
    SQObjectPtr res;
    if(SQ_SUCCEEDED(_sq_deleteslot(v,*self,*key,res))) {
        if(deleted) *deleted = res;
        return SQ_OK;
    }
    return SQ_ERROR;
}

static SQRESULT _sq_set(HSQUIRRELVM v,const SQObjectPtr& self,const SQObjectPtr& key,const SQObjectPtr& value)
{
    if(v->Set(self, key, value,DONT_FALL_BACK)) {
        return SQ_OK;
    }
    return SQ_ERROR;
}

SQRESULT sq_set(HSQUIRRELVM v,SQInteger idx)
{
    SQObjectPtr &self = stack_get(v, idx);
    if(SQ_SUCCEEDED(_sq_set(v, self, v->GetUp(-2), v->GetUp(-1)))) {
        v->Pop(2);
        return SQ_OK;
    }
    return SQ_ERROR;
}

SQRESULT sq_objset(HSQUIRRELVM v,const HSQOBJECT *self,const HSQOBJECT *key,const HSQOBJECT *value)
{
    return _sq_set(v,*self,*key,*value);
}

static SQRESULT _sq_rawset(HSQUIRRELVM v,const SQObjectPtr &self,const SQObjectPtr &key,const SQObjectPtr &value)
{
    if(sq_type(key) == OT_NULL) {
        return sq_throwerror(v, _SC("null key"));
    }
    switch(sq_type(self)) {
    case OT_TABLE:
        _table(self)->NewSlot(key, value);
        return SQ_OK;
    break;
    case OT_CLASS:
        _class(self)->NewSlot(_ss(v), key, value,false);
        return SQ_OK;
    break;
    case OT_INSTANCE:
        if(_instance(self)->Set(key, value)) {
            return SQ_OK;
        }
    break;
    case OT_ARRAY:
        if(v->Set(self, key, value,false)) {
            return SQ_OK;
        }
    break;
    default:
        return sq_throwerror(v, _SC("rawset works only on array/table/class and instance"));
    }
    v->Raise_IdxError(key);return SQ_ERROR;
}

SQRESULT sq_rawset(HSQUIRRELVM v,SQInteger idx)
{
    SQObjectPtr &self = stack_get(v, idx);
    SQObjectPtr &key = v->GetUp(-2);
    SQObjectPtr &value = v->GetUp(-1);
    SQRESULT res = _sq_rawset(v,self,key,value);
    v->Pop(2);
    return res;
}

SQRESULT sq_objrawset(HSQUIRRELVM v,const HSQOBJECT *self,const HSQOBJECT *key,const HSQOBJECT *value)
{
    return _sq_rawset(v,*self,*key,*value);
}

static SQRESULT _sq_newmember(HSQUIRRELVM v,const SQObjectPtr &self,const SQObjectPtr &key,const SQObjectPtr &value,const SQObjectPtr &attr,SQBool bstatic)
{
    if(sq_type(self) != OT_CLASS) return sq_throwerror(v, _SC("new member only works with classes"));
    if(sq_type(key) == OT_NULL) return sq_throwerror(v, _SC("null key"));
    if(!v->NewSlotA(self,key,value,attr,bstatic?true:false,false)) {
        return SQ_ERROR;
    }
    return SQ_OK;
}

SQRESULT sq_newmember(HSQUIRRELVM v,SQInteger idx,SQBool bstatic)
{
    SQObjectPtr &self = stack_get(v, idx);
    if(sq_type(self) != OT_CLASS) return sq_throwerror(v, _SC("new member only works with classes"));
    SQObjectPtr &key = v->GetUp(-3);
    if(sq_type(key) == OT_NULL) return sq_throwerror(v, _SC("null key"));
    SQObjectPtr &value = v->GetUp(-2);
    SQObjectPtr &attr = v->GetUp(-1);
    SQRESULT res = _sq_newmember(v,self,key,value,attr,bstatic);
    v->Pop(3);
    return res;
}

SQRESULT sq_objnewmember(HSQUIRRELVM v,const HSQOBJECT *self,const HSQOBJECT *key,const HSQOBJECT *value,const HSQOBJECT *attr,SQBool bstatic)
{
    return _sq_newmember(v,*self,*key,*value,*attr,bstatic);
}

static SQRESULT _sq_rawnewmember(HSQUIRRELVM v,const SQObjectPtr &self,const SQObjectPtr &key,const SQObjectPtr &value,const SQObjectPtr &attr,SQBool bstatic)
{
    if(sq_type(self) != OT_CLASS) return sq_throwerror(v, _SC("new member only works with classes"));
    if(sq_type(key) == OT_NULL) return sq_throwerror(v, _SC("null key"));
    if(!v->NewSlotA(self,key,value,attr,bstatic?true:false,true)) {
        return SQ_ERROR;
    }
    return SQ_OK;
}

SQRESULT sq_rawnewmember(HSQUIRRELVM v,SQInteger idx,SQBool bstatic)
{
    SQObjectPtr &self = stack_get(v, idx);
    if(sq_type(self) != OT_CLASS) return sq_throwerror(v, _SC("new member only works with classes"));
    SQObjectPtr &key = v->GetUp(-3);
    if(sq_type(key) == OT_NULL) return sq_throwerror(v, _SC("null key"));
    SQObjectPtr &value = v->GetUp(-2);
    SQObjectPtr &attr = v->GetUp(-1);
    SQRESULT res = _sq_rawnewmember(v,self,key,value,attr,bstatic);
    v->Pop(3);
    return res;
}

SQRESULT sq_objrawnewmember(HSQUIRRELVM v,const HSQOBJECT *self,const HSQOBJECT *key,const HSQOBJECT *value,const HSQOBJECT *attr,SQBool bstatic)
{
    return _sq_rawnewmember(v,*self,*key,*value,*attr,bstatic);
}

static SQRESULT _sq_setdelegate(HSQUIRRELVM v,const SQObjectPtr &self,const SQObjectPtr &mt)
{
    SQObjectType type = sq_type(self);
    switch(type) {
    case OT_TABLE:
        if(sq_type(mt) == OT_TABLE) {
            if(!_table(self)->SetDelegate(_table(mt))) {
                return sq_throwerror(v, _SC("delegate cycle"));
            }
        }
        else if(sq_type(mt)==OT_NULL) {
            _table(self)->SetDelegate(NULL); }
        else return sq_aux_invalidtype(v,type);
        break;
    case OT_USERDATA:
        if(sq_type(mt)==OT_TABLE) {
            _userdata(self)->SetDelegate(_table(mt)); }
        else if(sq_type(mt)==OT_NULL) {
            _userdata(self)->SetDelegate(NULL); }
        else return sq_aux_invalidtype(v, type);
        break;
    default:
            return sq_aux_invalidtype(v, type);
        break;
    }
    return SQ_OK;
}

SQRESULT sq_setdelegate(HSQUIRRELVM v,SQInteger idx)
{
    SQObjectPtr &self = stack_get(v, idx);
    SQObjectPtr &mt = v->GetUp(-1);
    if(SQ_SUCCEEDED(_sq_setdelegate(v,self,mt))) {
        v->Pop();
        return SQ_OK;
    }
    return SQ_ERROR;
}

SQRESULT sq_objsetdelegate(HSQUIRRELVM v,const HSQOBJECT *self,const HSQOBJECT *mt)
{
    return _sq_setdelegate(v,*self,*mt);
}

static SQRESULT _sq_rawdeleteslot(HSQUIRRELVM v,const SQObjectPtr &self,const SQObjectPtr &key,SQObjectPtr &deleted)
{
    if(sq_istable(self)) {
        if(_table(self)->Get(key,deleted)) {
            _table(self)->Remove(key);
        }
        return SQ_OK;
    }
    return SQ_ERROR;
}

SQRESULT sq_rawdeleteslot(HSQUIRRELVM v,SQInteger idx,SQBool pushval)
{
    sq_aux_paramscheck(v, 2);
    SQObjectPtr &self = stack_get(v, idx);
    SQObjectPtr &key = v->GetUp(-1);
    SQObjectPtr t;
    if(SQ_FAILED(_sq_rawdeleteslot(v,self,key,t))) {
        return SQ_ERROR;
    }
    if(pushval != 0)
        v->GetUp(-1) = t;
    else
        v->Pop();
    return SQ_OK;
}

SQRESULT sq_objrawdeleteslot(HSQUIRRELVM v,const HSQOBJECT *self,const HSQOBJECT *key,HSQOBJECT *deleted)
{
    SQObjectPtr t;
    if(SQ_SUCCEEDED(_sq_rawdeleteslot(v,*self,*key,t))) {
        if(deleted) *deleted = t;
        return SQ_OK;
    }
    return SQ_ERROR;
}

static SQRESULT _sq_getdelegate(HSQUIRRELVM v,const SQObjectPtr &self,SQObjectPtr &value)
{
    switch(sq_type(self)) {
    case OT_TABLE:
    case OT_USERDATA:
        if(!_delegable(self)->_delegate){
            value = SQObjectPtr();
            break;
        }
        value = SQObjectPtr(_delegable(self)->_delegate);
        break;
    default: return sq_throwerror(v,_SC("wrong type")); break;
    }
    return SQ_OK;
}

SQRESULT sq_getdelegate(HSQUIRRELVM v,SQInteger idx)
{
    SQObjectPtr &self=stack_get(v,idx);
    SQObjectPtr o;
    if(SQ_SUCCEEDED(_sq_getdelegate(v,self,o))) {
        v->Push(o);
        return SQ_OK;
    }
    return SQ_ERROR;
}

SQRESULT sq_objgetdelegate(HSQUIRRELVM v,const HSQOBJECT *self,HSQOBJECT *value)
{
    SQObjectPtr val;
    if(SQ_SUCCEEDED(_sq_getdelegate(v,*self,val))) {
        *value = val;
        return SQ_OK;
    }
    return SQ_ERROR;
}

static SQRESULT _sq_get(HSQUIRRELVM v,const SQObjectPtr& self,const SQObjectPtr& key,SQObjectPtr& obj)
{
    if(v->Get(self,key,obj,false,DONT_FALL_BACK)) {
        return SQ_OK;
    }
    return SQ_ERROR;
}

SQRESULT sq_get(HSQUIRRELVM v,SQInteger idx)
{
    SQObjectPtr &self=stack_get(v,idx);
    SQObjectPtr &obj = v->GetUp(-1);
    if(SQ_FAILED(_sq_get(v,self,obj,obj))) {
        v->Pop();
        return SQ_ERROR;
    }
    return SQ_OK;
}

SQRESULT sq_objget(HSQUIRRELVM v,const HSQOBJECT *self,const HSQOBJECT *key,HSQOBJECT *obj)
{
    SQObjectPtr dest;
    if(SQ_SUCCEEDED(_sq_get(v,*self,*key,dest))) {
        *obj = dest;
        return SQ_OK;
    }
    return SQ_ERROR;
}

static SQRESULT _sq_rawget(HSQUIRRELVM v,const SQObjectPtr &self,const SQObjectPtr &key,SQObjectPtr &value)
{
    switch(sq_type(self)) {
    case OT_TABLE:
        if(_table(self)->Get(key,value)) {
            return SQ_OK;
        }
        break;
    case OT_CLASS:
        if(_class(self)->Get(key,value)) {
            return SQ_OK;
        }
        break;
    case OT_INSTANCE:
        if(_instance(self)->Get(key,value)) {
            return SQ_OK;
        }
        break;
    case OT_ARRAY:{
        if(sq_isnumeric(key)){
            if(_array(self)->Get(tointeger(key),value)) {
                return SQ_OK;
            }
        }
        else {
            return sq_throwerror(v,_SC("invalid index type for an array"));
        }
                  }
        break;
    default:
        return sq_throwerror(v,_SC("rawget works only on array/table/instance and class"));
    }
    return sq_throwerror(v,_SC("the index doesn't exist"));
}

SQRESULT sq_rawget(HSQUIRRELVM v,SQInteger idx)
{
    SQObjectPtr &self=stack_get(v,idx);
    SQObjectPtr &obj = v->GetUp(-1);
    if(SQ_FAILED(_sq_rawget(v,self,obj,obj))) {
        v->Pop();
        return SQ_ERROR;
    }
    return SQ_OK;
}

SQRESULT sq_objrawget(HSQUIRRELVM v,const HSQOBJECT *self,const HSQOBJECT *key,HSQOBJECT *value)
{
    SQObjectPtr obj;
    if(SQ_SUCCEEDED(_sq_rawget(v,*self,*key,obj))) {
        *value = obj;
        return SQ_OK;
    }
    return SQ_ERROR;
}

SQRESULT sq_getstackobj(HSQUIRRELVM v,SQInteger idx,HSQOBJECT *po)
{
    *po=stack_get(v,idx);
    return SQ_OK;
}

const SQChar *sq_getlocal(HSQUIRRELVM v,SQUnsignedInteger level,SQUnsignedInteger idx)
{
    SQUnsignedInteger cstksize=v->_callsstacksize;
    SQUnsignedInteger lvl=(cstksize-level)-1;
    SQInteger stackbase=v->_stackbase;
    if(lvl<cstksize){
        for(SQUnsignedInteger i=0;i<level;i++){
            SQVM::CallInfo &ci=v->_callsstack[(cstksize-i)-1];
            stackbase-=ci._prevstkbase;
        }
        SQVM::CallInfo &ci=v->_callsstack[lvl];
        if(sq_type(ci._closure)!=OT_CLOSURE)
            return NULL;
        SQClosure *c=_closure(ci._closure);
        SQFunctionProto *func=c->_function;
        if(func->_noutervalues > (SQInteger)idx) {
            v->Push(*_outer(c->_outervalues[idx])->_valptr);
            return _stringval(func->_outervalues[idx]._name);
        }
        idx -= func->_noutervalues;
        return func->GetLocal(v,stackbase,idx,(SQInteger)(ci._ip-func->_instructions)-1);
    }
    return NULL;
}

void sq_pushobject(HSQUIRRELVM v,HSQOBJECT obj)
{
    v->Push(SQObjectPtr(obj));
}

void sq_resetobject(HSQOBJECT *po)
{
    po->_unVal.pUserPointer=NULL;po->_type=OT_NULL;
}

SQRESULT sq_throwerror(HSQUIRRELVM v,const SQChar *err)
{
    v->_lasterror=SQString::Create(_ss(v),err);
    return SQ_ERROR;
}

SQRESULT sq_throwobject(HSQUIRRELVM v)
{
    v->_lasterror = v->GetUp(-1);
    v->Pop();
    return SQ_ERROR;
}


void sq_reseterror(HSQUIRRELVM v)
{
    v->_lasterror.Null();
}

void sq_getlasterror(HSQUIRRELVM v)
{
    v->Push(v->_lasterror);
}

SQRESULT sq_reservestack(HSQUIRRELVM v,SQInteger nsize)
{
    if (((SQUnsignedInteger)v->_top + nsize) > v->_stack.size()) {
        if(v->_nmetamethodscall) {
            return sq_throwerror(v,_SC("cannot resize stack while in a metamethod"));
        }
        v->_stack.resize(v->_stack.size() + ((v->_top + nsize) - v->_stack.size()));
    }
    return SQ_OK;
}

SQRESULT sq_resume(HSQUIRRELVM v,SQBool retval,SQBool raiseerror)
{
    if (sq_type(v->GetUp(-1)) == OT_GENERATOR)
    {
        v->PushNull(); //retval
        if (!v->Execute(v->GetUp(-2), 0, v->_top, v->GetUp(-1), raiseerror, SQVM::ET_RESUME_GENERATOR))
        {v->Raise_Error(v->_lasterror); return SQ_ERROR;}
        if(!retval)
            v->Pop();
        return SQ_OK;
    }
    return sq_throwerror(v,_SC("only generators can be resumed"));
}

SQRESULT sq_call(HSQUIRRELVM v,SQInteger params,SQBool retval,SQBool raiseerror)
{
    SQObjectPtr res;
    if(!v->Call(v->GetUp(-(params+1)),params,v->_top-params,res,raiseerror?true:false)){
        v->Pop(params); //pop args
        return SQ_ERROR;
    }
    if(!v->_suspended)
        v->Pop(params); //pop args
    if(retval)
        v->Push(res); // push result
    return SQ_OK;
}

SQRESULT sq_tailcall(HSQUIRRELVM v, SQInteger nparams)
{
	SQObjectPtr &res = v->GetUp(-(nparams + 1));
	if (sq_type(res) != OT_CLOSURE) {
		return sq_throwerror(v, _SC("only closure can be tail called"));
	}
	SQClosure *clo = _closure(res);
	if (clo->_function->_bgenerator)
	{
		return sq_throwerror(v, _SC("generators cannot be tail called"));
	}
	
	SQInteger stackbase = (v->_top - nparams) - v->_stackbase;
	if (!v->TailCall(clo, stackbase, nparams)) {
		return SQ_ERROR;
	}
	return SQ_TAILCALL_FLAG;
}

SQRESULT sq_suspendvm(HSQUIRRELVM v)
{
    return v->Suspend();
}

SQRESULT sq_wakeupvm(HSQUIRRELVM v,SQBool wakeupret,SQBool retval,SQBool raiseerror,SQBool throwerror)
{
    SQObjectPtr ret;
    if(!v->_suspended)
        return sq_throwerror(v,_SC("cannot resume a vm that is not running any code"));
    SQInteger target = v->_suspended_target;
    if(wakeupret) {
        if(target != -1) {
            v->GetAt(v->_stackbase+v->_suspended_target)=v->GetUp(-1); //retval
        }
        v->Pop();
    } else if(target != -1) { v->GetAt(v->_stackbase+v->_suspended_target).Null(); }
    SQObjectPtr dummy;
    if(!v->Execute(dummy,-1,-1,ret,raiseerror,throwerror?SQVM::ET_RESUME_THROW_VM : SQVM::ET_RESUME_VM)) {
        return SQ_ERROR;
    }
    if(retval)
        v->Push(ret);
    return SQ_OK;
}

void sq_setreleasehook(HSQUIRRELVM v,SQInteger idx,SQRELEASEHOOK hook)
{
    SQObjectPtr &ud=stack_get(v,idx);
    switch(sq_type(ud) ) {
    case OT_USERDATA:   _userdata(ud)->_hook = hook;    break;
    case OT_INSTANCE:   _instance(ud)->_hook = hook;    break;
    case OT_CLASS:      _class(ud)->_hook = hook;       break;
    default: return;
    }
}

SQRELEASEHOOK sq_getreleasehook(HSQUIRRELVM v,SQInteger idx)
{
    SQObjectPtr &ud=stack_get(v,idx);
    switch(sq_type(ud) ) {
    case OT_USERDATA:   return _userdata(ud)->_hook;    break;
    case OT_INSTANCE:   return _instance(ud)->_hook;    break;
    case OT_CLASS:      return _class(ud)->_hook;       break;
    default: return NULL;
    }
}

void sq_setcompilererrorhandler(HSQUIRRELVM v,SQCOMPILERERROR f)
{
    _ss(v)->_compilererrorhandler = f;
}

SQRESULT sq_writeclosure(HSQUIRRELVM v,SQWRITEFUNC w,SQUserPointer up)
{
    SQObjectPtr *o = NULL;
    _GETSAFE_OBJ(v, -1, OT_CLOSURE,o);
    unsigned short tag = SQ_BYTECODE_STREAM_TAG;
    if(_closure(*o)->_function->_noutervalues)
        return sq_throwerror(v,_SC("a closure with free variables bound cannot be serialized"));
    if(w(up,&tag,2) != 2)
        return sq_throwerror(v,_SC("io error"));
    if(!_closure(*o)->Save(v,up,w))
        return SQ_ERROR;
    return SQ_OK;
}

SQRESULT sq_readclosure(HSQUIRRELVM v,SQREADFUNC r,SQUserPointer up)
{
    SQObjectPtr closure;

    unsigned short tag;
    if(r(up,&tag,2) != 2)
        return sq_throwerror(v,_SC("io error"));
    if(tag != SQ_BYTECODE_STREAM_TAG)
        return sq_throwerror(v,_SC("invalid stream"));
    if(!SQClosure::Load(v,up,r,closure))
        return SQ_ERROR;
    v->Push(closure);
    return SQ_OK;
}

SQChar *sq_getscratchpad(HSQUIRRELVM v,SQInteger minsize)
{
    return _ss(v)->GetScratchPad(minsize);
}

SQRESULT sq_resurrectunreachable(HSQUIRRELVM v)
{
#ifndef NO_GARBAGE_COLLECTOR
    _ss(v)->ResurrectUnreachable(v);
    return SQ_OK;
#else
    return sq_throwerror(v,_SC("sq_resurrectunreachable requires a garbage collector build"));
#endif
}

SQInteger sq_collectgarbage(HSQUIRRELVM v)
{
#ifndef NO_GARBAGE_COLLECTOR
    return _ss(v)->CollectGarbage(v);
#else
    return -1;
#endif
}

SQRESULT sq_getcallee(HSQUIRRELVM v)
{
    if(v->_callsstacksize > 1)
    {
        v->Push(v->_callsstack[v->_callsstacksize - 2]._closure);
        return SQ_OK;
    }
    return sq_throwerror(v,_SC("no closure in the calls stack"));
}

const SQChar *sq_getfreevariable(HSQUIRRELVM v,SQInteger idx,SQUnsignedInteger nval)
{
    SQObjectPtr &self=stack_get(v,idx);
    const SQChar *name = NULL;
    switch(sq_type(self))
    {
    case OT_CLOSURE:{
        SQClosure *clo = _closure(self);
        SQFunctionProto *fp = clo->_function;
        if(((SQUnsignedInteger)fp->_noutervalues) > nval) {
            v->Push(*(_outer(clo->_outervalues[nval])->_valptr));
            SQOuterVar &ov = fp->_outervalues[nval];
            name = _stringval(ov._name);
        }
                    }
        break;
    case OT_NATIVECLOSURE:{
        SQNativeClosure *clo = _nativeclosure(self);
        if(clo->_noutervalues > nval) {
            v->Push(clo->_outervalues[nval]);
            name = _SC("@NATIVE");
        }
                          }
        break;
    default: break; //shutup compiler
    }
    return name;
}

SQRESULT sq_setfreevariable(HSQUIRRELVM v,SQInteger idx,SQUnsignedInteger nval)
{
    SQObjectPtr &self=stack_get(v,idx);
    switch(sq_type(self))
    {
    case OT_CLOSURE:{
        SQFunctionProto *fp = _closure(self)->_function;
        if(((SQUnsignedInteger)fp->_noutervalues) > nval){
            *(_outer(_closure(self)->_outervalues[nval])->_valptr) = stack_get(v,-1);
        }
        else return sq_throwerror(v,_SC("invalid free var index"));
                    }
        break;
    case OT_NATIVECLOSURE:
        if(_nativeclosure(self)->_noutervalues > nval){
            _nativeclosure(self)->_outervalues[nval] = stack_get(v,-1);
        }
        else return sq_throwerror(v,_SC("invalid free var index"));
        break;
    default:
        return sq_aux_invalidtype(v, sq_type(self));
    }
    v->Pop();
    return SQ_OK;
}

SQRESULT sq_setattributes(HSQUIRRELVM v,SQInteger idx)
{
    SQObjectPtr *o = NULL;
    _GETSAFE_OBJ(v, idx, OT_CLASS,o);
    SQObjectPtr &key = stack_get(v,-2);
    SQObjectPtr &val = stack_get(v,-1);
    SQObjectPtr attrs;
    if(sq_type(key) == OT_NULL) {
        attrs = _class(*o)->_attributes;
        _class(*o)->_attributes = val;
        v->Pop(2);
        v->Push(attrs);
        return SQ_OK;
    }else if(_class(*o)->GetAttributes(key,attrs)) {
        _class(*o)->SetAttributes(key,val);
        v->Pop(2);
        v->Push(attrs);
        return SQ_OK;
    }
    return sq_throwerror(v,_SC("wrong index"));
}

static SQRESULT _sq_getattributes(HSQUIRRELVM v,const SQObjectPtr &o,const SQObjectPtr &key,SQObjectPtr &value)
{
    if(sq_isclass(o)) {
        if(sq_type(key) == OT_NULL) {
            value = _class(o)->_attributes;
            return SQ_OK;
        }
        else if(_class(o)->GetAttributes(key,value)) {
            return SQ_OK;
        }
        return sq_throwerror(v,_SC("wrong index"));
    }
    return SQ_ERROR;
}

SQRESULT sq_getattributes(HSQUIRRELVM v,SQInteger idx)
{
    SQObjectPtr &o = stack_get(v, idx);
    SQObjectPtr &key = stack_get(v,-1);
    return _sq_getattributes(v,o,key,key);
}

SQRESULT sq_objgetattributes(HSQUIRRELVM v,const HSQOBJECT *o,const HSQOBJECT *key,HSQOBJECT *value)
{
    SQObjectPtr attrs;
    if(SQ_SUCCEEDED(_sq_getattributes(v,*o,*key,attrs))) {
        *value = attrs;
        return SQ_OK;
    }
    return SQ_ERROR;
}

SQRESULT sq_getmemberhandle(HSQUIRRELVM v,SQInteger idx,HSQMEMBERHANDLE *handle)
{
    SQObjectPtr *o = NULL;
    _GETSAFE_OBJ(v, idx, OT_CLASS,o);
    SQObjectPtr &key = stack_get(v,-1);
    SQTable *m = _class(*o)->_members;
    SQObjectPtr val;
    if(m->Get(key,val)) {
        handle->_static = _isfield(val) ? SQFalse : SQTrue;
        handle->_index = _member_idx(val);
        v->Pop();
        return SQ_OK;
    }
    return sq_throwerror(v,_SC("wrong index"));
}

SQRESULT _getmemberbyhandle(HSQUIRRELVM v,SQObjectPtr &self,const HSQMEMBERHANDLE *handle,SQObjectPtr *&val)
{
    switch(sq_type(self)) {
        case OT_INSTANCE: {
                SQInstance *i = _instance(self);
                if(handle->_static) {
                    SQClass *c = i->_class;
                    val = &c->_methods[handle->_index].val;
                }
                else {
                    val = &i->_values[handle->_index];

                }
            }
            break;
        case OT_CLASS: {
                SQClass *c = _class(self);
                if(handle->_static) {
                    val = &c->_methods[handle->_index].val;
                }
                else {
                    val = &c->_defaultvalues[handle->_index].val;
                }
            }
            break;
        default:
            return sq_throwerror(v,_SC("wrong type(expected class or instance)"));
    }
    return SQ_OK;
}

SQRESULT sq_getbyhandle(HSQUIRRELVM v,SQInteger idx,const HSQMEMBERHANDLE *handle)
{
    SQObjectPtr &self = stack_get(v,idx);
    SQObjectPtr *val = NULL;
    if(SQ_FAILED(_getmemberbyhandle(v,self,handle,val))) {
        return SQ_ERROR;
    }
    v->Push(_realval(*val));
    return SQ_OK;
}

SQRESULT sq_setbyhandle(HSQUIRRELVM v,SQInteger idx,const HSQMEMBERHANDLE *handle)
{
    SQObjectPtr &self = stack_get(v,idx);
    SQObjectPtr &newval = stack_get(v,-1);
    SQObjectPtr *val = NULL;
    if(SQ_FAILED(_getmemberbyhandle(v,self,handle,val))) {
        return SQ_ERROR;
    }
    *val = newval;
    v->Pop();
    return SQ_OK;
}

static SQRESULT _sq_getbase(HSQUIRRELVM v,const SQObjectPtr &o,SQObjectPtr &value)
{
    if(sq_isclass(o)) {
        if(_class(o)->_base)
            value = SQObjectPtr(_class(o)->_base);
        else
            value = SQObjectPtr();
        return SQ_OK;
    }
    return SQ_ERROR;
}

SQRESULT sq_getbase(HSQUIRRELVM v,SQInteger idx)
{
    SQObjectPtr &o = stack_get(v,idx);
    SQObjectPtr base;
    _sq_getbase(v,o,base);
    v->Push(base);
    return SQ_OK;
}

SQRESULT sq_objgetbase(HSQUIRRELVM v,const HSQOBJECT *o,HSQOBJECT *value)
{
    SQObjectPtr obj;
    if(SQ_SUCCEEDED(_sq_getbase(v,o,obj))) {
        *value = obj;
        return SQ_OK;
    }
    return SQ_ERROR;
}

static SQRESULT _sq_getclass(HSQUIRRELVM v,const SQObjectPtr &o,SQObjectPtr &value)
{
    if(sq_isinstance(o)) {
        value = SQObjectPtr(_instance(o)->_class);
        return SQ_OK;
    }
    return SQ_ERROR;
}

SQRESULT sq_getclass(HSQUIRRELVM v,SQInteger idx)
{
    SQObjectPtr &o = stack_get(v, idx);
    SQObjectPtr val;
    if(SQ_SUCCEEDED(_sq_getclass(v,o,val))) {
        v->Push(val);
        return SQ_OK;
    }
    return SQ_ERROR;
}

SQRESULT sq_objgetclass(HSQUIRRELVM v,const HSQOBJECT *o,HSQOBJECT *value)
{
    if(sq_isinstance(*o)) {
        *value = SQObjectPtr(_instance(*o)->_class);
        return SQ_OK;
    }
    return SQ_ERROR;
}

SQRESULT sq_createinstance(HSQUIRRELVM v,SQInteger idx)
{
    SQObjectPtr *o = NULL;
    _GETSAFE_OBJ(v, idx, OT_CLASS,o);
    v->Push(_class(*o)->CreateInstance());
    return SQ_OK;
}

void sq_weakref(HSQUIRRELVM v,SQInteger idx)
{
    SQObject &o=stack_get(v,idx);
    if(ISREFCOUNTED(sq_type(o))) {
        v->Push(_refcounted(o)->GetWeakRef(sq_type(o)));
        return;
    }
    v->Push(o);
}

SQRESULT sq_getweakrefval(HSQUIRRELVM v,SQInteger idx)
{
    SQObjectPtr &o = stack_get(v,idx);
    if(sq_type(o) != OT_WEAKREF) {
        return sq_throwerror(v,_SC("the object must be a weakref"));
    }
    v->Push(_weakref(o)->_obj);
    return SQ_OK;
}

SQRESULT sq_getdefaultdelegate(HSQUIRRELVM v,SQObjectType t)
{
    SQSharedState *ss = _ss(v);
    switch(t) {
    case OT_TABLE: v->Push(ss->_table_default_delegate); break;
    case OT_ARRAY: v->Push(ss->_array_default_delegate); break;
    case OT_STRING: v->Push(ss->_string_default_delegate); break;
    case OT_INTEGER: case OT_FLOAT: v->Push(ss->_number_default_delegate); break;
    case OT_GENERATOR: v->Push(ss->_generator_default_delegate); break;
    case OT_CLOSURE: case OT_NATIVECLOSURE: v->Push(ss->_closure_default_delegate); break;
    case OT_THREAD: v->Push(ss->_thread_default_delegate); break;
    case OT_CLASS: v->Push(ss->_class_default_delegate); break;
    case OT_INSTANCE: v->Push(ss->_instance_default_delegate); break;
    case OT_WEAKREF: v->Push(ss->_weakref_default_delegate); break;
    default: return sq_throwerror(v,_SC("the type doesn't have a default delegate"));
    }
    return SQ_OK;
}

SQRESULT sq_next(HSQUIRRELVM v,SQInteger idx)
{
    SQObjectPtr o=stack_get(v,idx),&refpos = stack_get(v,-1),realkey,val;
    if(sq_type(o) == OT_GENERATOR) {
        return sq_throwerror(v,_SC("cannot iterate a generator"));
    }
    int faketojump;
    if(!v->FOREACH_OP(o,realkey,val,refpos,0,666,faketojump))
        return SQ_ERROR;
    if(faketojump != 666) {
        v->Push(realkey);
        v->Push(val);
        return SQ_OK;
    }
    return SQ_ERROR;
}

struct BufState{
    const SQChar *buf;
    SQInteger ptr;
    SQInteger size;
};

SQInteger buf_lexfeed(SQUserPointer file)
{
    BufState *buf=(BufState*)file;
    if(buf->size<(buf->ptr+1))
        return 0;
    return buf->buf[buf->ptr++];
}

SQRESULT sq_compilebuffer(HSQUIRRELVM v,const SQChar *s,SQInteger size,const SQChar *sourcename,SQBool raiseerror) {
    BufState buf;
    buf.buf = s;
    buf.size = size;
    buf.ptr = 0;
    return sq_compile(v, buf_lexfeed, &buf, sourcename, raiseerror);
}

void sq_move(HSQUIRRELVM dest,HSQUIRRELVM src,SQInteger idx)
{
    dest->Push(stack_get(src,idx));
}

void sq_setprintfunc(HSQUIRRELVM v, SQPRINTFUNCTION printfunc,SQPRINTFUNCTION errfunc)
{
    _ss(v)->_printfunc = printfunc;
    _ss(v)->_errorfunc = errfunc;
}

SQPRINTFUNCTION sq_getprintfunc(HSQUIRRELVM v)
{
    return _ss(v)->_printfunc;
}

SQPRINTFUNCTION sq_geterrorfunc(HSQUIRRELVM v)
{
    return _ss(v)->_errorfunc;
}

void *sq_malloc(SQUnsignedInteger size)
{
    return SQ_MALLOC(size);
}

void *sq_realloc(void* p,SQUnsignedInteger oldsize,SQUnsignedInteger newsize)
{
    return SQ_REALLOC(p,oldsize,newsize);
}

void sq_free(void *p,SQUnsignedInteger size)
{
    SQ_FREE(p,size);
}
