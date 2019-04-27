#include <stdlib.h>

#include "gsc_saveposition.hpp"

struct osjh_save {
	osjh_save *prevsave;
	gentity_t *groundentity;
	vec3_t origin;
	vec3_t angles;
    int checkPointId;
    int RPGJumps;
    int nadeJumps;
    int doubleRPGs;
};

static osjh_save *playersaves[MAX_CLIENTS];
static osjh_save *playersaves_selected[MAX_CLIENTS];

void gsc_saveposition_initclient(int id)
{
	osjh_save *prev = playersaves[id];
	while(prev != NULL)
	{
		osjh_save *tmp = prev->prevsave;
		free(prev);
		prev = tmp;
	}
	playersaves[id] = NULL;
	playersaves_selected[id] = NULL;
}

void gsc_saveposition_save(int id) //player savePosition_save(origin, angles, entity)
{
	osjh_save *newsave = (osjh_save*)malloc(sizeof(osjh_save));
	if(newsave == NULL)
	{
		stackPushUndefined();
		return;
	}
    
	stackGetParamVector(0, newsave->origin);
	stackGetParamVector(1, newsave->angles);
	if(stackGetParamType(2) == STACK_INT)
	{
		int entnum;
		stackGetParamInt(2, &entnum);
		newsave->groundentity = &g_entities[entnum];
	}
	else
    {
		newsave->groundentity = NULL;
    }
    stackGetParamInt(3, &newsave->RPGJumps);
    stackGetParamInt(4, &newsave->nadeJumps);
    stackGetParamInt(5, &newsave->doubleRPGs);
    if(stackGetParamType(6) == STACK_INT)
    {
        stackGetParamInt(6, &newsave->checkPointId);
    }
    else
    {
        newsave->checkPointId = -1;
    }
    
	newsave->prevsave = playersaves[id];
	playersaves[id] = newsave;
    
	stackPushInt(0);
}

void gsc_saveposition_selectsave(int id) //player savePosition_selectSave(backwardsCount)
{
	int backwardscount;
	stackGetParamInt(0, &backwardscount);
	if(playersaves[id] == NULL)
	{
		stackPushInt(1);
		return;
	}
	playersaves_selected[id] = playersaves[id];

	while(backwardscount)
	{
		if(playersaves_selected[id]->prevsave != NULL)
		{
			playersaves_selected[id] = playersaves_selected[id]->prevsave;
		}
		else
		{
			stackPushInt(2);
			return;
		}
		backwardscount--;
	}
    
	stackPushInt(0);
}

void gsc_saveposition_getangles(int id)
{
	stackPushVector(playersaves_selected[id]->angles);
}

void gsc_saveposition_getorigin(int id)
{
	stackPushVector(playersaves_selected[id]->origin);
}

void gsc_saveposition_getgroundentity(int id)
{
	if(playersaves_selected[id]->groundentity == NULL)
		stackPushUndefined();
	else
		stackPushEntity(playersaves_selected[id]->groundentity);
}

void gsc_saveposition_getnadejumps(int id)
{
    stackPushInt(playersaves_selected[id]->nadeJumps);
}

void gsc_saveposition_getrpgjumps(int id)
{
    stackPushInt(playersaves_selected[id]->RPGJumps);
}

void gsc_saveposition_getdoublerpg(int id)
{
    stackPushInt(playersaves_selected[id]->doubleRPGs);
}

void gsc_saveposition_getcheckpointid(int id)
{
    if(playersaves_selected[id]->checkPointId != -1)
    {
			printf("pushing %d\n", playersaves_selected[id]->checkPointId);
        stackPushInt(playersaves_selected[id]->checkPointId);
    }
    else
    {
			printf("pushing undef\n");
        stackPushUndefined();
    }
}
