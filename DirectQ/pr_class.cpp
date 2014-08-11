/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

 
 
*/

#include "quakedef.h"
#include "pr_class.h"

// 2001-09-14 Enhanced BuiltIn Function System (EBFS) by Maddes  start
cvar_t	pr_builtin_find ("pr_builtin_find", "0");
cvar_t	pr_builtin_remap ("pr_builtin_remap", "0");
// 2001-09-14 Enhanced BuiltIn Function System (EBFS) by Maddes  end


char *pr_opnames[] =
{
	"DONE", "MUL_F", "MUL_V", "MUL_FV", "MUL_VF", "DIV", "ADD_F", "ADD_V", "SUB_F", "SUB_V", "EQ_F", "EQ_V", "EQ_S", 
	"EQ_E", "EQ_FNC", "NE_F", "NE_V", "NE_S", "NE_E", "NE_FNC", "LE", "GE", "LT", "GT", "INDIRECT", "INDIRECT",
	"INDIRECT", "INDIRECT", "INDIRECT", "INDIRECT", "ADDRESS", "STORE_F", "STORE_V", "STORE_S", "STORE_ENT",
	"STORE_FLD", "STORE_FNC", "STOREP_F", "STOREP_V", "STOREP_S", "STOREP_ENT", "STOREP_FLD", "STOREP_FNC",
	"RETURN", "NOT_F", "NOT_V", "NOT_S", "NOT_ENT", "NOT_FNC", "IF", "IFNOT", "CALL0", "CALL1", "CALL2", "CALL3",
	"CALL4", "CALL5", "CALL6", "CALL7", "CALL8", "STATE", "GOTO", "AND", "OR", "BITAND", "BITOR"
};


char *PR_GlobalString (int ofs);
char *PR_GlobalStringNoContents (int ofs);


void FindEdictFieldOffsets (void);


void *CProgsDat::LoadProgsLump (byte *lumpbegin, int lumplen, int lumpitemsize)
{
	byte *lumpdata = (byte *) Pool_Map->Alloc (lumplen * lumpitemsize);
	memcpy (lumpdata, lumpbegin, lumplen * lumpitemsize);
	return lumpdata;
}


CProgsDat::CProgsDat (void)
{
	// set up the stack
	this->Stack = (prstack_t *) Pool_Map->Alloc ((MAX_STACK_DEPTH + 1) * sizeof (prstack_t));
	this->StackDepth = 0;

	this->LocalStack = (int *) Pool_Map->Alloc (LOCALSTACK_SIZE * sizeof (int));
	this->LocalStackUsed = 0;
}


void CProgsDat::LoadProgs (char *progsname, cvar_t *overridecvar)
{
// 2001-09-14 Enhanced BuiltIn Function System (EBFS) by Maddes/Firestorm  start
	int 	j;
	int		funcno;
	char	*funcname;
// 2001-09-14 Enhanced BuiltIn Function System (EBFS) by Maddes/Firestorm  end

	// this can't be in a constructor yet because we need the instance already created in order to do
	// FindEdictFieldOffsets without crashing.  When we get everything on the server gone to OOP we'll
	// do it right.
	// flush the non-C variable lookup cache
	for (int i = 0; i < GEFV_CACHESIZE; i++) gefvCache[i].field[0] = 0;

	CRC_Init (&this->CRC);

	// attempt to load it from sv_progs cvar first
	int progslen;
	HANDLE progshandle = INVALID_HANDLE_VALUE;

	if (overridecvar) progslen = COM_FOpenFile (overridecvar->string, &progshandle);

	if (progshandle == INVALID_HANDLE_VALUE)
	{
		// fall back on regular progs.dat
		progslen = COM_FOpenFile (progsname, &progshandle);

		if (progshandle == INVALID_HANDLE_VALUE)
		{
			Host_Error ("CProgsDat::CProgsDat: couldn't load progs.dat");
			return;
		}
	}

	Con_DPrintf ("Programs occupy %iK.\n", progslen / 1024);

	// because progs seems prone to heap corruption errors (fixme - why?) we load the header
	// into a non-pointer struct and the lumps into their own individual buffers to make it more robust
	// this fixes the crash bug with game changing followed by map load
	Pool_Temp->Rewind ();
	dprograms_t *progs = (dprograms_t *) Pool_Temp->Alloc (progslen);

	// read it all in
	int rlen = COM_FReadFile (progshandle, progs, progslen);

	// done with the file
	COM_FCloseFile (&progshandle);

	if (rlen != progslen) Host_Error ("CProgsDat::CProgsDat: not enough data read");

	// CRC the progs
	for (int i = 0; i < progslen; i++)
		CRC_ProcessByte (&this->CRC, ((byte *) progs)[i]);

	// byte swap the header
	for (int i = 0; i < sizeof (dprograms_t) / 4; i++)
		((int *) progs)[i] = LittleLong (((int *) progs)[i]);

	if (progs->version != PROG_VERSION) Host_Error ("progs.dat has wrong version number (%i should be %i)", progs->version, PROG_VERSION);
	if (progs->crc != PROGHEADER_CRC) Host_Error ("progs.dat system vars have been modified, progdefs.h is out of date");

	// copy out to the global progs header structure and set up a byte * pointer so that we can
	// more cleanly reference the loaded progs
	memcpy (&this->QC, progs, sizeof (dprograms_t));
	byte *progsdata = (byte *) progs;

	// load all the progs lumps into thir own separate memory locations rather than keeping them in the single
	// contiguous lump
	this->Functions = (dfunction_t *) this->LoadProgsLump (progsdata + this->QC.ofs_functions, this->QC.numfunctions, sizeof (dfunction_t));
	this->Strings = (char *) this->LoadProgsLump (progsdata + this->QC.ofs_strings, this->QC.numstrings, 1);
	this->GlobalDefs = (ddef_t *) this->LoadProgsLump (progsdata + this->QC.ofs_globaldefs, this->QC.numglobaldefs, sizeof (ddef_t));
	this->FieldDefs = (ddef_t *) this->LoadProgsLump (progsdata + this->QC.ofs_fielddefs, this->QC.numfielddefs, sizeof (ddef_t));
	this->Statements = (dstatement_t *) this->LoadProgsLump (progsdata + this->QC.ofs_statements, this->QC.numstatements, sizeof (dstatement_t));
	this->Globals = (float *) this->LoadProgsLump (progsdata + this->QC.ofs_globals, this->QC.numglobals, sizeof (float));

	// this just points at this->Globals (per comment above on it's declaration)
	this->GlobalStruct = (globalvars_t *) this->Globals;
	this->EdictSize = this->QC.entityfields * 4 + sizeof (edict_t) - sizeof (entvars_t);

	// byte swap the lumps
	for (int i = 0; i < this->QC.numstatements; i++)
	{
		this->Statements[i].op = LittleShort (this->Statements[i].op);
		this->Statements[i].a = LittleShort (this->Statements[i].a);
		this->Statements[i].b = LittleShort (this->Statements[i].b);
		this->Statements[i].c = LittleShort (this->Statements[i].c);
	}

// 2001-09-14 Enhanced BuiltIn Function System (EBFS) by Maddes/Firestorm  start
	// initialize function numbers for PROGS.DAT
	pr_numbuiltins = 0;
	pr_builtins = NULL;

	if (pr_builtin_remap.value)
	{
		// remove all previous assigned function numbers
		for (j = 1; j < pr_ebfs_numbuiltins; j++)
		{
			pr_ebfs_builtins[j].funcno = 0;
		}
	}
	else
	{
		// use default function numbers
		for (j = 1; j < pr_ebfs_numbuiltins; j++)
		{
			pr_ebfs_builtins[j].funcno = pr_ebfs_builtins[j].default_funcno;

			// determine highest builtin number (when NOT remapped)
			if (pr_ebfs_builtins[j].funcno > pr_numbuiltins)
			{
				pr_numbuiltins = pr_ebfs_builtins[j].funcno;
			}
		}
	}
// 2001-09-14 Enhanced BuiltIn Function System (EBFS) by Maddes/Firestorm  end

	for (int i = 0; i < this->QC.numfunctions; i++)
	{
		this->Functions[i].first_statement = LittleLong (this->Functions[i].first_statement);
		this->Functions[i].parm_start = LittleLong (this->Functions[i].parm_start);
		this->Functions[i].s_name = LittleLong (this->Functions[i].s_name);
		this->Functions[i].s_file = LittleLong (this->Functions[i].s_file);
		this->Functions[i].numparms = LittleLong (this->Functions[i].numparms);
		this->Functions[i].locals = LittleLong (this->Functions[i].locals);

// 2001-09-14 Enhanced BuiltIn Function System (EBFS) by Maddes/Firestorm  start
		if (pr_builtin_remap.value)
		{
			if (this->Functions[i].first_statement < 0)	// builtin function
			{
				funcno = -this->Functions[i].first_statement;
				funcname = this->Strings + this->Functions[i].s_name;

				// search function name
				for (j = 1; j < pr_ebfs_numbuiltins; j++)
				{
					if (!(stricmp (funcname, pr_ebfs_builtins[j].funcname)))
					{
						break;	// found
					}
				}

				if (j < pr_ebfs_numbuiltins)	// found
				{
					pr_ebfs_builtins[j].funcno = funcno;
				}
				else
				{
					Con_DPrintf ("Can not assign builtin number #%i to %s - function unknown\n", funcno, funcname);
				}
			}
		}
// 2001-09-14 Enhanced BuiltIn Function System (EBFS) by Maddes/Firestorm  end
	}

// 2001-09-14 Enhanced BuiltIn Function System (EBFS) by Maddes/Firestorm  start
	if (pr_builtin_remap.value)
	{
		// check for unassigned functions and try to assign their default function number
		for (int i = 1; i < pr_ebfs_numbuiltins; i++)
		{
			if ((!pr_ebfs_builtins[i].funcno) && (pr_ebfs_builtins[i].default_funcno))	// unassigned and has a default number
			{
				// check if default number is already assigned to another function
				for (j = 1; j < pr_ebfs_numbuiltins; j++)
				{
					if (pr_ebfs_builtins[j].funcno == pr_ebfs_builtins[i].default_funcno)
					{
						break;	// number already assigned to another builtin function
					}
				}

				if (j < pr_ebfs_numbuiltins)	// already assigned
				{
					Con_DPrintf
					(
						"Can not assign default builtin number #%i to %s - number is already assigned to %s\n",
						pr_ebfs_builtins[i].default_funcno, pr_ebfs_builtins[i].funcname, pr_ebfs_builtins[j].funcname
					);
				}
				else
				{
					pr_ebfs_builtins[i].funcno = pr_ebfs_builtins[i].default_funcno;
				}
			}

			// determine highest builtin number (when remapped)
			if (pr_ebfs_builtins[i].funcno > pr_numbuiltins)
			{
				pr_numbuiltins = pr_ebfs_builtins[i].funcno;
			}
		}
	}

	pr_numbuiltins++;

	// allocate and initialize builtin list for execution time
	pr_builtins = (builtin_t *) Pool_Map->Alloc (pr_numbuiltins * sizeof (builtin_t));

	for (int i = 0; i < pr_numbuiltins; i++)
	{
		pr_builtins[i] = pr_ebfs_builtins[0].function;
	}

	// create builtin list for execution time and set cvars accordingly
	Cvar_Set ("pr_builtin_find", "0");
	Cvar_Set ("pr_checkextension", "0");	// 2001-10-20 Extension System by Lord Havoc/Maddes (DP compatibility)

	for (j = 1; j < pr_ebfs_numbuiltins; j++)
	{
		if (pr_ebfs_builtins[j].funcno)	// only put assigned functions into builtin list
		{
			pr_builtins[pr_ebfs_builtins[j].funcno] = pr_ebfs_builtins[j].function;
		}

		if (pr_ebfs_builtins[j].default_funcno == PR_DEFAULT_FUNCNO_BUILTIN_FIND)
		{
			Cvar_Set ("pr_builtin_find", pr_ebfs_builtins[j].funcno);
		}

// 2001-10-20 Extension System by Lord Havoc/Maddes (DP compatibility)  start
		if (pr_ebfs_builtins[j].default_funcno == PR_DEFAULT_FUNCNO_EXTENSION_FIND)
		{
			Cvar_Set ("pr_checkextension", pr_ebfs_builtins[j].funcno);
		}
// 2001-10-20 Extension System by Lord Havoc/Maddes (DP compatibility)  end
	}
// 2001-09-14 Enhanced BuiltIn Function System (EBFS) by Maddes/Firestorm  end

	for (int i = 0; i < this->QC.numglobaldefs; i++)
	{
		this->GlobalDefs[i].type = LittleShort (this->GlobalDefs[i].type);
		this->GlobalDefs[i].ofs = LittleShort (this->GlobalDefs[i].ofs);
		this->GlobalDefs[i].s_name = LittleLong (this->GlobalDefs[i].s_name);
	}

	for (int i = 0; i < this->QC.numfielddefs; i++)
	{
		this->FieldDefs[i].type = LittleShort (this->FieldDefs[i].type);

		if (this->FieldDefs[i].type & DEF_SAVEGLOBAL)
			Host_Error ("CProgsDat::CProgsDat: this->FieldDefs[i].type & DEF_SAVEGLOBAL");

		this->FieldDefs[i].ofs = LittleShort (this->FieldDefs[i].ofs);
		this->FieldDefs[i].s_name = LittleLong (this->FieldDefs[i].s_name);
	}

	for (int i = 0; i < this->QC.numglobals; i++)
		((int *) this->Globals)[i] = LittleLong (((int *) this->Globals)[i]);

	FindEdictFieldOffsets ();
}


CProgsDat::~CProgsDat (void)
{
}


void CProgsDat::ExecuteProgram (func_t fnum)
{
	eval_t		*a, *b, *c;
	int		s;
	dstatement_t	*st;
	dfunction_t	*f, *newf;
	int		runaway;
	int		i;
	edict_t		*ed;
	int		exitdepth;
	eval_t		*ptr;

	if (!fnum || fnum < 0 || fnum >= this->QC.numfunctions)
	{
		if (this->GlobalStruct->self)
			ED_Print (PROG_TO_EDICT (this->GlobalStruct->self));

		if (!fnum)
			Host_Error ("CProgsDat::ExecuteProgram: NULL function");
		else if (fnum < 0)
			Host_Error ("CProgsDat::ExecuteProgram: fnum < 0");
		else Host_Error ("CProgsDat::ExecuteProgram: fnum >= this->QC.numfunctions");
	}

	f = &this->Functions[fnum];
	char *fname = this->Strings + f->s_name;
	char *ffile = this->Strings + f->s_file;

	runaway = 5000000;
	this->Trace = false;

	// make a stack frame
	exitdepth = this->StackDepth;

	s = this->EnterFunction (f);

	while (1)
	{
		s++;	// next statement

		if (s >= this->QC.numstatements) Host_Error ("CProgsDat::ExecuteProgram: s >= this->QC.numstatements");

		this->XStatement = s;
		this->XFunction->profile++;

		st = &this->Statements[s];
		a = (eval_t *)&this->Globals[(unsigned short)st->a];
		b = (eval_t *)&this->Globals[(unsigned short)st->b];
		c = (eval_t *)&this->Globals[(unsigned short)st->c];

		if (!--runaway) this->RunError ("runaway loop error %d");
		if (this->Trace) this->PrintStatement (st);

		switch (st->op)
		{
		case OP_ADD_F:
			c->_float = a->_float + b->_float;
			break;
		case OP_ADD_V:
			c->vector[0] = a->vector[0] + b->vector[0];
			c->vector[1] = a->vector[1] + b->vector[1];
			c->vector[2] = a->vector[2] + b->vector[2];
			break;

		case OP_SUB_F:
			c->_float = a->_float - b->_float;
			break;
		case OP_SUB_V:
			c->vector[0] = a->vector[0] - b->vector[0];
			c->vector[1] = a->vector[1] - b->vector[1];
			c->vector[2] = a->vector[2] - b->vector[2];
			break;

		case OP_MUL_F:
			c->_float = a->_float * b->_float;
			break;
		case OP_MUL_V:
			c->_float = a->vector[0]*b->vector[0]
					+ a->vector[1]*b->vector[1]
					+ a->vector[2]*b->vector[2];
			break;
		case OP_MUL_FV:
			c->vector[0] = a->_float * b->vector[0];
			c->vector[1] = a->_float * b->vector[1];
			c->vector[2] = a->_float * b->vector[2];
			break;
		case OP_MUL_VF:
			c->vector[0] = b->_float * a->vector[0];
			c->vector[1] = b->_float * a->vector[1];
			c->vector[2] = b->_float * a->vector[2];
			break;
		case OP_DIV_F:
			c->_float = a->_float / b->_float;
			break;
		case OP_BITAND:
			c->_float = (int)a->_float & (int)b->_float;
			break;
		case OP_BITOR:
			c->_float = (int)a->_float | (int)b->_float;
			break;
		case OP_GE:
			c->_float = a->_float >= b->_float;
			break;
		case OP_LE:
			c->_float = a->_float <= b->_float;
			break;
		case OP_GT:
			c->_float = a->_float > b->_float;
			break;
		case OP_LT:
			c->_float = a->_float < b->_float;
			break;
		case OP_AND:
			c->_float = a->_float && b->_float;
			break;
		case OP_OR:
			c->_float = a->_float || b->_float;
			break;
		case OP_NOT_F:
			c->_float = !a->_float;
			break;
		case OP_NOT_V:
			c->_float = !a->vector[0] && !a->vector[1] && !a->vector[2];
			break;
		case OP_NOT_S:
			c->_float = !a->string || !this->Strings[a->string];
			break;
		case OP_NOT_FNC:
			c->_float = !a->function;
			break;
		case OP_NOT_ENT:
			c->_float = (PROG_TO_EDICT(a->edict) == this->Edicts);
			break;
		case OP_EQ_F:
			c->_float = a->_float == b->_float;
			break;
		case OP_EQ_V:
			c->_float = (a->vector[0] == b->vector[0]) &&
						(a->vector[1] == b->vector[1]) &&
						(a->vector[2] == b->vector[2]);
			break;
		case OP_EQ_S:
			c->_float = !strcmp(this->Strings+a->string,this->Strings+b->string);
			break;
		case OP_EQ_E:
			c->_float = a->_int == b->_int;
			break;
		case OP_EQ_FNC:
			c->_float = a->function == b->function;
			break;
		case OP_NE_F:
			c->_float = a->_float != b->_float;
			break;
		case OP_NE_V:
			c->_float = (a->vector[0] != b->vector[0]) ||
						(a->vector[1] != b->vector[1]) ||
						(a->vector[2] != b->vector[2]);
			break;
		case OP_NE_S:
			c->_float = strcmp(this->Strings+a->string,this->Strings+b->string);
			break;
		case OP_NE_E:
			c->_float = a->_int != b->_int;
			break;
		case OP_NE_FNC:
			c->_float = a->function != b->function;
			break;

	//==================
		case OP_STORE_F:
		case OP_STORE_ENT:
		case OP_STORE_FLD:		// integers
		case OP_STORE_S:
		case OP_STORE_FNC:		// pointers
			b->_int = a->_int;
			break;
		case OP_STORE_V:
			b->vector[0] = a->vector[0];
			b->vector[1] = a->vector[1];
			b->vector[2] = a->vector[2];
			break;

		case OP_STOREP_F:
		case OP_STOREP_ENT:
		case OP_STOREP_FLD:		// integers
		case OP_STOREP_S:
		case OP_STOREP_FNC:		// pointers
			ptr = (eval_t *)((byte *)this->Edicts + b->_int);
			ptr->_int = a->_int;
			break;
		case OP_STOREP_V:
			ptr = (eval_t *)((byte *)this->Edicts + b->_int);
			ptr->vector[0] = a->vector[0];
			ptr->vector[1] = a->vector[1];
			ptr->vector[2] = a->vector[2];
			break;

		case OP_ADDRESS:
			ed = PROG_TO_EDICT(a->edict);

			if (ed == (edict_t *)this->Edicts && sv.state == ss_active)
				this->RunError ("CProgsDat::ExecuteProgram: assignment to world entity");

			c->_int = (byte *)((int *)&ed->v + b->_int) - (byte *)this->Edicts;
			break;

		case OP_LOAD_F:
		case OP_LOAD_FLD:
		case OP_LOAD_ENT:
		case OP_LOAD_S:
		case OP_LOAD_FNC:
			ed = PROG_TO_EDICT(a->edict);

			a = (eval_t *)((int *)&ed->v + b->_int);
			c->_int = a->_int;
			break;

		case OP_LOAD_V:
			ed = PROG_TO_EDICT(a->edict);

			a = (eval_t *)((int *)&ed->v + b->_int);
			c->vector[0] = a->vector[0];
			c->vector[1] = a->vector[1];
			c->vector[2] = a->vector[2];
			break;

	//==================

		case OP_IFNOT:
			if (!a->_int)
				s += st->b - 1;	// offset the s++
			break;

		case OP_IF:
			if (a->_int)
				s += st->b - 1;	// offset the s++
			break;

		case OP_GOTO:
			s += st->a - 1;	// offset the s++
			break;

		case OP_CALL0:
		case OP_CALL1:
		case OP_CALL2:
		case OP_CALL3:
		case OP_CALL4:
		case OP_CALL5:
		case OP_CALL6:
		case OP_CALL7:
		case OP_CALL8:
			this->Argc = st->op - OP_CALL0;

			if (!a->function)
			{
				if ((st - 1)->op == OP_LOAD_FNC) // OK?
					ED_Print (ed); // Print owner edict, if any
				else if (this->GlobalStruct->self)
					ED_Print (PROG_TO_EDICT(this->GlobalStruct->self));

				this->RunError ("PR_ExecuteProgram2: NULL function");
			}

			newf = &this->Functions[a->function];

			if (newf->first_statement < 0)
			{
				char *blah = this->Strings + newf->s_name;

				// negative statements are built in functions
				i = -newf->first_statement;

				if (i >= pr_numbuiltins)
					this->RunError ("CProgsDat::ExecuteProgram: bad builtin call number (%d, max = %d)", i, pr_numbuiltins);

				pr_builtins[i] ();
				break;
			}

			s = this->EnterFunction (newf);
			break;

		case OP_DONE:
		case OP_RETURN:
			this->Globals[OFS_RETURN] = this->Globals[(unsigned short)st->a];
			this->Globals[OFS_RETURN+1] = this->Globals[(unsigned short)st->a+1];
			this->Globals[OFS_RETURN+2] = this->Globals[(unsigned short)st->a+2];

			s = this->LeaveFunction ();

			if (this->StackDepth == exitdepth)
			{
				return;		// all done
			}
			break;

		case OP_STATE:
			ed = PROG_TO_EDICT(this->GlobalStruct->self);
			ed->v.nextthink = this->GlobalStruct->time + 0.1;

			if (a->_float != ed->v.frame)
			{
				ed->v.frame = a->_float;
			}
			ed->v.think = b->function;
			break;

		default:
			this->RunError ("CProgsDat::ExecuteProgram: bad opcode %i", st->op);
		}
	}
}


int CProgsDat::EnterFunction (dfunction_t *f)
{
	int i, j, c, o;

	this->Stack[this->StackDepth].s = this->XStatement;
	this->Stack[this->StackDepth].f = this->XFunction;	
	this->StackDepth++;

	if (this->StackDepth >= MAX_STACK_DEPTH) this->RunError ("CProgsDat::EnterFunction: stack overflow (%d, max = %d)", this->StackDepth, MAX_STACK_DEPTH - 1);

	// save off any locals that the new function steps on
	c = f->locals;

	if (this->LocalStackUsed + c > LOCALSTACK_SIZE)
		this->RunError ("CProgsDat::EnterFunction: locals stack overflow (%d, max = %d)", this->LocalStackUsed + c, LOCALSTACK_SIZE);

	for (i = 0; i < c; i++)
		this->LocalStack[this->LocalStackUsed + i] = ((int *) this->Globals)[f->parm_start + i];

	this->LocalStackUsed += c;

	// copy parameters
	o = f->parm_start;

	for (i = 0; i < f->numparms; i++)
	{
		for (j = 0; j < f->parm_size[i]; j++)
		{
			((int *) this->Globals)[o] = ((int *) this->Globals)[OFS_PARM0 + i * 3 + j];
			o++;
		}
	}

	this->XFunction = f;
	return f->first_statement - 1;	// offset the s++
}


int CProgsDat::LeaveFunction (void)
{
	int		i, c;

	if (this->StackDepth <= 0)
		Host_Error ("CProgsDat::LeaveFunction: prog stack underflow");

	// restore locals from the stack
	c = this->XFunction->locals;
	this->LocalStackUsed -= c;

	if (this->LocalStackUsed < 0)
		this->RunError ("CProgsDat::LeaveFunction: locals stack underflow\n");

	for (i = 0; i < c; i++)
		((int *) this->Globals)[this->XFunction->parm_start + i] = this->LocalStack[this->LocalStackUsed + i];

	// up stack
	this->StackDepth--;

	this->XFunction = this->Stack[this->StackDepth].f;
	return this->Stack[this->StackDepth].s;
}


void CProgsDat::PrintStatement (dstatement_t *s)
{
	if ((unsigned) s->op < sizeof (pr_opnames) / sizeof (pr_opnames[0]))
		Con_SafePrintf ("%-10s ", pr_opnames[s->op]);

	if (s->op == OP_IF || s->op == OP_IFNOT)
		Con_SafePrintf ("%sbranch %i", PR_GlobalString ((unsigned short) s->a), s->b);
	else if (s->op == OP_GOTO)
		Con_SafePrintf ("branch %i", s->a);
	else if ((unsigned) (s->op - OP_STORE_F) < 6)
	{
		Con_SafePrintf ("%s", PR_GlobalString ((unsigned short) s->a));
		Con_SafePrintf ("%s", PR_GlobalStringNoContents ((unsigned short) s->b));
	}
	else
	{
		if (s->a) Con_SafePrintf ("%s", PR_GlobalString ((unsigned short) s->a));
		if (s->b) Con_SafePrintf ("%s", PR_GlobalString ((unsigned short) s->b));
		if (s->c) Con_SafePrintf ("%s", PR_GlobalStringNoContents ((unsigned short) s->c));
	}

	Con_SafePrintf ("\n");
}


void CProgsDat::RunError (char *error, ...)
{
	va_list		argptr;
	char		string[1024];

	va_start (argptr,error);
	_vsnprintf (string,1024, error,argptr);
	va_end (argptr);

	this->PrintStatement (this->Statements + this->XStatement);
	this->StackTrace ();
	Con_Printf ("%s\n", string);

	// dump the stack so host_error can shutdown functions
	this->StackDepth = 0;

	QC_DebugOutput ("CProgsDat::RunError: %s", string);
	Host_Error ("Program error");
}


void CProgsDat::Profile (void)
{
	dfunction_t	*f, *best;
	int			max;
	int			num;
	int			i;

	num = 0;	
	do
	{
		max = 0;
		best = NULL;

		for (i = 0; i < this->QC.numfunctions; i++)
		{
			f = &this->Functions[i];
			if (f->profile > max)
			{
				max = f->profile;
				best = f;
			}
		}
		if (best)
		{
			if (num < 10)
				Con_Printf ("%7i %s\n", best->profile, this->Strings + best->s_name);
			num++;
			best->profile = 0;
		}
	} while (best);
}


void CProgsDat::StackTrace (void)
{
	dfunction_t	*f;
	int			i;

	if (this->StackDepth == 0)
	{
		Con_Printf ("<NO STACK>\n");
		return;
	}

	if (this->StackDepth > MAX_STACK_DEPTH) this->StackDepth = MAX_STACK_DEPTH;

	this->Stack[this->StackDepth].s = this->XStatement;
	this->Stack[this->StackDepth].f = this->XFunction;

	for (i = this->StackDepth; i >= 0; i--)
	{
		f = this->Stack[i].f;

		if (f)
		{
			Con_Printf
			(
				"%12s : %s statement %i\n",
				this->Strings + f->s_file,
				this->Strings + f->s_name,
				this->Stack[i].s - f->first_statement
			);
		}
		else Con_Printf ("<NO FUNCTION>\n");
	}
}
