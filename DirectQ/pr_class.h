/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 3
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/


#pragma once
#ifndef PR_CLASS_H
#define PR_CLASS_H

#define GEFV_CACHESIZE	16	// bumped for more cached values
#define	MAX_FIELD_LEN	64

typedef struct
{
	ddef_t	*pcache;
	char	field[MAX_FIELD_LEN];
} gefv_cache;

extern gefv_cache gefvCache[];

typedef struct prstack_s
{
	int s;
	dfunction_t *f;
} prstack_t;

#define	MAX_STACK_DEPTH		2048
#define	LOCALSTACK_SIZE		16384


class CProgsDat
{
public:
	// default progs def
	dprograms_t		*QC;
	dfunction_t		*Functions;
	char			*Strings;
	ddef_t			*FieldDefs;
	ddef_t			*GlobalDefs;
	dstatement_t	*Statements;
	globalvars_t	*GlobalStruct;
	float			*Globals;			// same as SVProgs->GlobalStruct
	int				EdictSize;	// in bytes
	unsigned short	CRC;

	// string handling
	int StringSize;
	char **KnownStrings;
	int NumKnownStrings;
	int MaxKnownStrings;

	int AllocString (int bufferlength, char **ptr);
	char *GetString (int num);
	int SetString (char *s);

	// progs execution stack
	prstack_t *Stack;
	int StackDepth;

	int *LocalStack;
	int LocalStackUsed;

	dfunction_t *XFunction;
	int XStatement;

	bool Trace;
	int Argc;

	CProgsDat (void);
	~CProgsDat (void);

	void LoadProgs (char *progsname, cvar_t *overridecvar);

	// execution
	void ExecuteProgram (func_t fnum);
	int EnterFunction (dfunction_t *f);
	int LeaveFunction (void);
	void PrintStatement (dstatement_t *s);
	void RunError (char *error, ...);

	void Profile (void);
	void StackTrace (void);

	bool	FishHack;
	int		NumFish;

	// the edicts more properly belong to the progs than to the server
	// edict_t *Edicts;
	edict_t **EdictPointers;
	int NumEdicts;
	int MaxEdicts;

private:
	void AllocStringSlots (void);
};

extern CProgsDat *SVProgs;

#endif // PR_CLASS_H