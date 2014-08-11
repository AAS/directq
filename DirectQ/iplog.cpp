/*
Copyright (C) 2002, J.P. Grossman

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
// iplog.c
//
// JPG 1.05
//
// This entire file is new in proquake 1.05.  It is used for player IP logging.
//

/*
=======================
WHAT THE HOLY BLUE FUCK
=======================
*/

#include "quakedef.h"

CQuakeZone *IPLogZone = NULL;

#pragma pack (push, 1)
// this doesn't actually need to be in the header
typedef struct iplog_s
{
	// bug - int comparison will incorrectly place IPs > 127.x.x.x in the tree
	// this should really be unsigned int or byte[4]
	union
	{
		unsigned int addr;
		unsigned char octets[4];
	};

	char name[16];

	union
	{
		struct iplog_s *parent;
		struct iplog_s *next;
	};

	struct iplog_s *children[2];
} iplog_t;
#pragma pack (pop)

void IPLog_Delete (iplog_t *node);
iplog_t *IPLog_Merge (iplog_t *left, iplog_t *right);

iplog_t	*iplogs;
iplog_t *iplog_head;

int iplog_size;
int iplog_next;
int iplog_full;

#define DEFAULT_IPLOGSIZE	0x10000

char dequake[256];	// JPG 1.05


// dequake crap from ProQuake host.c; DirectQ doesn't have a dedicated server console
void Host_InitDeQuake (void)
{
	for (int i = 1; i < 12; i++)
		dequake[i] = '#';

	dequake[9] = 9;
	dequake[10] = 10;
	dequake[13] = 13;
	dequake[12] = ' ';
	dequake[1] = dequake[5] = dequake[14] = dequake[15] = dequake[28] = '.';
	dequake[16] = '[';
	dequake[17] = ']';

	for (int i = 0; i < 10; i++)
		dequake[18 + i] = '0' + i;

	dequake[29] = '<';
	dequake[30] = '-';
	dequake[31] = '>';

	for (int i = 32; i < 128; i++)
		dequake[i] = i;

	for (int i = 0; i < 128; i++)
		dequake[i + 128] = dequake[i];

	dequake[128] = '(';
	dequake[129] = '=';
	dequake[130] = ')';
	dequake[131] = '*';
	dequake[141] = '>';
}


/*
====================
IPLog_Init
====================
*/
void IPLog_Init (void)
{
	FILE *f;
	iplog_t temp;

	// ewwwww
	Host_InitDeQuake ();

	// Allocate space for the IP logs
	iplog_size = 0;

	int p = (!COM_CheckParm ("-noiplog"));

	if (!p) return;
	if (p < com_argc - 1) iplog_size = atoi (com_argv[p + 1]) * 1024 / sizeof (iplog_t);
	if (!iplog_size) iplog_size = DEFAULT_IPLOGSIZE;

	// in theory this gives us unlimited log space; in practice we'll need to untangle the messy tree structure to get there
	IPLogZone = new CQuakeZone ();
	iplogs = (iplog_t *) IPLogZone->Alloc (iplog_size * sizeof (iplog_t));
	iplog_next = 0;
	iplog_head = NULL;
	iplog_full = 0;

	// Attempt to load log data from iplog.dat
	f = fopen (va ("%s/id1/iplog.dat", host_parms.basedir), "r"); // Baker 3.83 ... todo: double check that -basedir operates as expected

	if (f)
	{
		while (fread (&temp, 20, 1, f))
			IPLog_Add (temp.addr, temp.name);

		fclose (f);
	}
}


/*
====================
IPLog_Import
====================
*/
void IPLog_Import_f (void)
{
	FILE *f;
	iplog_t temp;

	if (!iplog_size)
	{
		Con_Printf ("IP logging not available\nRemove -noiplog command line option\n"); // Baker 3.83: Now -iplog is the default
		return;
	}

	if (Cmd_Argc () < 2)
	{
		Con_Printf ("Usage: ipmerge <filename>\n");
		return;
	}

	if ((f = fopen (va ("%s", Cmd_Argv (1)), "r")) != NULL)
	{
		while (fread (&temp, 20, 1, f))
			IPLog_Add (temp.addr, temp.name);

		fclose (f);
		Con_Printf ("Merged %s\n", Cmd_Argv (1));
	}
	else Con_Printf ("Could not open %s\n", Cmd_Argv (1));
}

/*
====================
IPLog_WriteLog
====================
*/
void IPLog_WriteLog (void)
{
	FILE *f;
	int i;
	iplog_t temp;

	if (!iplog_size) return;

	// first merge
	if ((f = fopen (va ("%s/id1/iplog.dat", host_parms.basedir), "r")) != NULL)
	{
		while (fread (&temp, 20, 1, f))
			IPLog_Add (temp.addr, temp.name);

		fclose (f);
	}

	// see have we anything to write
	// the iplog structure doesn't make this particularly pleasant for us
	int numlogs = 0;

	if (iplog_full)
		for (i = iplog_next + 1; i < iplog_size; i++, numlogs++);

	for (i = 0; i < iplog_next; i++, numlogs++);

	// if we have nothing to say then we should bloody well say nothing!
	if (numlogs)
	{
		// then write
		if ((f = fopen (va ("%s/id1/iplog.dat", host_parms.basedir), "w")) != NULL)
		{
			if (iplog_full)
				for (i = iplog_next + 1; i < iplog_size; i++)
					fwrite (&iplogs[i], 20, 1, f);

			for (i = 0; i < iplog_next; i++)
				fwrite (&iplogs[i], 20, 1, f);

			// this happens on shutdown so it shouldn't attempt to Con_Printf anything
			fclose (f);
		}
	}
}

#define MAX_REPITITION	64

/*
====================
IPLog_Add
====================
*/
void IPLog_Add (int addr, char *name)
{
	iplog_t	*iplog_new;
	iplog_t **ppnew;
	iplog_t *parent;
	char name2[16];
	char *ch;
	int cmatch;		// limit MAX_REPITITION entries per IP
	iplog_t *match[MAX_REPITITION];
	int i;

	if (!iplog_size)
		return;

	// delete trailing spaces
	strncpy (name2, name, 15);
	ch = &name2[15];
	*ch = 0;

	while (ch >= name2 && (*ch == 0 || *ch == ' '))
		*ch-- = 0;

	if (ch < name2)
		return;

	iplog_new = &iplogs[iplog_next];

	cmatch = 0;
	parent = NULL;
	ppnew = &iplog_head;

	// looks for a matching name/address and replaces it
	while (*ppnew)
	{
		if ((*ppnew)->addr == addr)
		{
			if (!strcmp (name2, (*ppnew)->name))
				return;

			match[cmatch] = *ppnew;

			if (++cmatch == MAX_REPITITION)
			{
				// shift up the names and replace the last one
				for (i = 0; i < MAX_REPITITION - 1; i++)
					strcpy (match[i]->name, match[i+1]->name);

				strcpy (match[i]->name, name2);
				return;
			}
		}

		parent = *ppnew;
		ppnew = &(*ppnew)->children[addr > (*ppnew)->addr];
	}

	*ppnew = iplog_new;
	strcpy (iplog_new->name, name2);
	iplog_new->addr = addr;
	iplog_new->parent = parent;
	iplog_new->children[0] = NULL;
	iplog_new->children[1] = NULL;

	if (++iplog_next == iplog_size)
	{
		iplog_next = 0;
		iplog_full = 1;
	}

	if (iplog_full) IPLog_Delete (&iplogs[iplog_next]);
}


/*
====================
IPLog_Delete
====================
*/
void IPLog_Delete (iplog_t *node)
{
	iplog_t *newlog = IPLog_Merge (node->children[0], node->children[1]);

	if (newlog)
		newlog->parent = node->parent;

	if (node->parent)
		node->parent->children[node->addr > node->parent->addr] = newlog;
	else iplog_head = newlog;
}


/*
====================
IPLog_Merge
====================
*/
iplog_t *IPLog_Merge (iplog_t *left, iplog_t *right)
{
	if (!left) return right;
	if (!right) return left;

	if (rand () & 1)
	{
		left->children[1] = IPLog_Merge (left->children[1], right);
		left->children[1]->parent = left;
		return left;
	}

	right->children[0] = IPLog_Merge (left, right->children[0]);
	right->children[0]->parent = right;
	return right;
}

/*
====================
IPLog_Identify
====================
*/
void IPLog_Identify (int addr)
{
	iplog_t *node;
	int count = 0;

	node = iplog_head;

	while (node)
	{
		if (node->addr == addr)
		{
			Con_Printf ("%s\n", node->name);
			count++;
		}

		node = node->children[addr > node->addr];
	}

	Con_Printf ("%d %s found\n", count, (count == 1) ? "entry" : "entries");
}

/*
====================
IPLog_DumpTree
====================
*/
void IPLog_DumpTree (iplog_t *root, FILE *f)
{
	char address[16];
	char name[16];
	unsigned char *ch;

	if (!root)
		return;

	IPLog_DumpTree (root->children[0], f);

	_snprintf (address, sizeof (address), "%d.%d.%d.xxx", root->addr >> 16, (root->addr >> 8) & 0xff, root->addr & 0xff);
	strcpy (name, root->name);

	// C++ comaptibility
	unsigned char *name2 = (unsigned char *) name;

	for (ch = name2; *ch; ch++)
	{
		*ch = dequake[*ch];

		if (*ch == 10 || *ch == 13)
			*ch = ' ';
	}

	fprintf (f, "%-16s  %s\n", address, name);

	IPLog_DumpTree (root->children[1], f);
}

/*
====================
IPLog_Dump
====================
*/
void IPLog_Dump_f (void)
{
	FILE *f;

	if (!iplog_size)
	{
		Con_Printf ("IP logging not available\nRemove -noiplog command line option\n"); // Baker 3.83: Now -iplog is the default
		return;
	}

	f = fopen (va ("%s/id1/iplog.txt", host_parms.basedir), "w");

	if (!f)
	{
		Con_Printf ("Couldn't write iplog.txt.\n");
		return;
	}

	IPLog_DumpTree (iplog_head, f);
	fclose (f);
	Con_Printf ("Wrote iplog.txt\n");
}

