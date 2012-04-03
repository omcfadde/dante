/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company.

This file is part of the Doom 3 GPL Source Code (?Doom 3 Source Code?).

Doom 3 Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "tr_local.h"

shaderProgram_t	interactionShader;
shaderProgram_t	shadowShader;

/*
=========================================================================================

GENERAL INTERACTION RENDERING

=========================================================================================
*/

/*
====================
GL_SelectTextureNoClient
====================
*/
static void GL_SelectTextureNoClient(int unit)
{
	backEnd.glState.currenttmu = unit;
	glActiveTextureARB(GL_TEXTURE0_ARB + unit);
	RB_LogComment("glActiveTextureARB( %i )\n", unit);
}


/*
====================
RB_GLSL_SelectInteractionUniforms
====================
*/
static void RB_GLSL_SelectInteractionUniforms(const drawInteraction_t *din,
		shaderProgram_t *shader)
{
	static const float zero[4] = { 0, 0, 0, 0 };
	static const float one[4] = { 1, 1, 1, 1 };
	static const float negOne[4] = { -1, -1, -1, -1 };

	glUniform4fvARB(shader->localLightOrigin, 1, din->localLightOrigin.ToFloatPtr());
	glUniform4fvARB(shader->localViewOrigin, 1, din->localViewOrigin.ToFloatPtr());
	glUniform4fvARB(shader->lightProjectionS, 1, din->lightProjection[0].ToFloatPtr());
	glUniform4fvARB(shader->lightProjectionT, 1, din->lightProjection[1].ToFloatPtr());
	glUniform4fvARB(shader->lightProjectionQ, 1, din->lightProjection[2].ToFloatPtr());
	glUniform4fvARB(shader->lightFalloff , 1, din->lightProjection[3].ToFloatPtr());
	glUniform4fvARB(shader->bumpMatrixS, 1, din->bumpMatrix[0].ToFloatPtr());
	glUniform4fvARB(shader->bumpMatrixT, 1, din->bumpMatrix[1].ToFloatPtr());
	glUniform4fvARB(shader->diffuseMatrixS, 1, din->diffuseMatrix[0].ToFloatPtr());
	glUniform4fvARB(shader->diffuseMatrixT, 1, din->diffuseMatrix[1].ToFloatPtr());
	glUniform4fvARB(shader->specularMatrixS, 1, din->specularMatrix[0].ToFloatPtr());
	glUniform4fvARB(shader->specularMatrixT, 1, din->specularMatrix[1].ToFloatPtr());

	switch (din->vertexColor) {
		case SVC_IGNORE:
			glUniform4fvARB(shader->colorModulate, 1, zero);
			glUniform4fvARB(shader->colorAdd, 1, one);
			break;
		case SVC_MODULATE:
			glUniform4fvARB(shader->colorModulate, 1, one);
			glUniform4fvARB(shader->colorAdd, 1, zero);
			break;
		case SVC_INVERSE_MODULATE:
			glUniform4fvARB(shader->colorModulate, 1, negOne);
			glUniform4fvARB(shader->colorAdd, 1, one);
			break;
	}

	// set the constant colors
	glUniform4fvARB(shader->diffuseColor, 1, din->diffuseColor.ToFloatPtr());
	glUniform4fvARB(shader->specularColor, 1, din->specularColor.ToFloatPtr());
}

/*
==================
RB_GLSL_DrawInteraction
==================
*/
void	RB_GLSL_DrawInteraction(const drawInteraction_t *din)
{
	// load all the vertex program parameters
	RB_GLSL_SelectInteractionUniforms(din, &interactionShader);

	// set the textures

	// texture 0 will be the per-surface bump map
	GL_SelectTextureNoClient(0);
	din->bumpImage->Bind();

	// texture 1 will be the light falloff texture
	GL_SelectTextureNoClient(1);
	din->lightFalloffImage->Bind();

	// texture 2 will be the light projection texture
	GL_SelectTextureNoClient(2);
	din->lightImage->Bind();

	// texture 3 is the per-surface diffuse map
	GL_SelectTextureNoClient(3);
	din->diffuseImage->Bind();

	// texture 4 is the per-surface specular map
	GL_SelectTextureNoClient(4);
	din->specularImage->Bind();

	// draw it
	RB_DrawElementsWithCounters(din->surf->geo);
}


/*
=============
RB_GLSL_CreateDrawInteractions

=============
*/
void RB_GLSL_CreateDrawInteractions(const drawSurf_t *surf)
{
	if (!surf) {
		return;
	}

	// perform setup here that will be constant for all interactions
	GL_State(GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHMASK | backEnd.depthFunc);

	// bind the vertex and fragment shader
	glUseProgramObjectARB(interactionShader.program);

	// enable the vertex arrays
	glEnableVertexAttribArrayARB(8);
	glEnableVertexAttribArrayARB(9);
	glEnableVertexAttribArrayARB(10);
	glEnableVertexAttribArrayARB(11);
	glEnableClientState(GL_COLOR_ARRAY);

	// texture 5 is the specular lookup table
	GL_SelectTextureNoClient(5);
	globalImages->specularTableImage->Bind();

	for (; surf ; surf=surf->nextOnLight) {
		// perform setup here that will not change over multiple interaction passes

		// set the vertex pointers
		idDrawVert	*ac = (idDrawVert *)vertexCache.Position(surf->geo->ambientCache);
		glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(idDrawVert), ac->color);
		glVertexAttribPointerARB(11, 3, GL_FLOAT, false, sizeof(idDrawVert), ac->normal.ToFloatPtr());
		glVertexAttribPointerARB(10, 3, GL_FLOAT, false, sizeof(idDrawVert), ac->tangents[1].ToFloatPtr());
		glVertexAttribPointerARB(9, 3, GL_FLOAT, false, sizeof(idDrawVert), ac->tangents[0].ToFloatPtr());
		glVertexAttribPointerARB(8, 2, GL_FLOAT, false, sizeof(idDrawVert), ac->st.ToFloatPtr());
		glVertexPointer(3, GL_FLOAT, sizeof(idDrawVert), ac->xyz.ToFloatPtr());

		glUniformMatrix4fvARB(interactionShader.modelMatrix, 1, false, surf->space->modelMatrix);

		// this may cause RB_GLSL_DrawInteraction to be exacuted multiple
		// times with different colors and images if the surface or light have multiple layers
		RB_CreateSingleDrawInteractions(surf, RB_GLSL_DrawInteraction);
	}

	glDisableVertexAttribArrayARB(8);
	glDisableVertexAttribArrayARB(9);
	glDisableVertexAttribArrayARB(10);
	glDisableVertexAttribArrayARB(11);
	glDisableClientState(GL_COLOR_ARRAY);

	// disable features
	GL_SelectTextureNoClient(5);
	globalImages->BindNull();

	GL_SelectTextureNoClient(4);
	globalImages->BindNull();

	GL_SelectTextureNoClient(3);
	globalImages->BindNull();

	GL_SelectTextureNoClient(2);
	globalImages->BindNull();

	GL_SelectTextureNoClient(1);
	globalImages->BindNull();

	backEnd.glState.currenttmu = -1;
	GL_SelectTexture(0);

	glUseProgramObjectARB(0);
}


/*
==================
RB_GLSL_DrawInteractions
==================
*/
void RB_GLSL_DrawInteractions(void)
{
	viewLight_t		*vLight;
	const idMaterial	*lightShader;

	GL_SelectTexture(0);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);

	//
	// for each light, perform adding and shadowing
	//
	for (vLight = backEnd.viewDef->viewLights ; vLight ; vLight = vLight->next) {
		backEnd.vLight = vLight;

		// do fogging later
		if (vLight->lightShader->IsFogLight()) {
			continue;
		}

		if (vLight->lightShader->IsBlendLight()) {
			continue;
		}

		if (!vLight->localInteractions && !vLight->globalInteractions
		    && !vLight->translucentInteractions) {
			continue;
		}

		lightShader = vLight->lightShader;

		// clear the stencil buffer if needed
		if (vLight->globalShadows || vLight->localShadows) {
			backEnd.currentScissor = vLight->scissorRect;

			if (r_useScissor.GetBool()) {
				glScissor(backEnd.viewDef->viewport.x1 + backEnd.currentScissor.x1,
				           backEnd.viewDef->viewport.y1 + backEnd.currentScissor.y1,
				           backEnd.currentScissor.x2 + 1 - backEnd.currentScissor.x1,
				           backEnd.currentScissor.y2 + 1 - backEnd.currentScissor.y1);
			}

			glClear(GL_STENCIL_BUFFER_BIT);
		} else {
			// no shadows, so no need to read or write the stencil buffer
			// we might in theory want to use GL_ALWAYS instead of disabling
			// completely, to satisfy the invarience rules
			glStencilFunc(GL_ALWAYS, 128, 255);
		}

		if (r_useShadowVertexProgram.GetBool()) {
			glUseProgramObjectARB(shadowShader.program);
			RB_StencilShadowPass(vLight->globalShadows);
			RB_GLSL_CreateDrawInteractions(vLight->localInteractions);

			glUseProgramObjectARB(shadowShader.program);
			RB_StencilShadowPass(vLight->localShadows);
			RB_GLSL_CreateDrawInteractions(vLight->globalInteractions);
			glUseProgramObjectARB(0);	// if there weren't any globalInteractions, it would have stayed on
		} else {
			RB_StencilShadowPass(vLight->globalShadows);
			RB_GLSL_CreateDrawInteractions(vLight->localInteractions);
			RB_StencilShadowPass(vLight->localShadows);
			RB_GLSL_CreateDrawInteractions(vLight->globalInteractions);
		}

		// translucent surfaces never get stencil shadowed
		if (r_skipTranslucent.GetBool()) {
			continue;
		}

		glStencilFunc(GL_ALWAYS, 128, 255);

		backEnd.depthFunc = GLS_DEPTHFUNC_LESS;
		RB_GLSL_CreateDrawInteractions(vLight->translucentInteractions);

		backEnd.depthFunc = GLS_DEPTHFUNC_EQUAL;
	}

	// disable stencil shadow test
	glStencilFunc(GL_ALWAYS, 128, 255);

	GL_SelectTexture(0);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
}

//===================================================================================


/*
=================
R_LoadGLSLShader

loads GLSL vertex or fragment shaders
=================
*/
static void R_LoadGLSLShader(const char *name, shaderProgram_t *shaderProgram, GLenum type)
{
	idStr	fullPath = "gl2progs/";
	fullPath += name;
	char	*fileBuffer;
	char	*buffer;

	common->Printf("%s", fullPath.c_str());

	// load the program even if we don't support it, so
	// fs_copyfiles can generate cross-platform data dumps
	fileSystem->ReadFile(fullPath.c_str(), (void **)&fileBuffer, NULL);

	if (!fileBuffer) {
		common->Printf(": File not found\n");
		return;
	}

	// copy to stack memory and free
	buffer = (char *)_alloca(strlen(fileBuffer) + 1);
	strcpy(buffer, fileBuffer);
	fileSystem->FreeFile(fileBuffer);

	if (!glConfig.isInitialized) {
		return;
	}

	switch (type) {
		case GL_VERTEX_SHADER_ARB:
			// create vertex shader
			shaderProgram->vertexShader = glCreateShaderObjectARB(GL_VERTEX_SHADER_ARB);
			glShaderSourceARB(shaderProgram->vertexShader, 1, (const GLcharARB **)&buffer, 0);
			glCompileShaderARB(shaderProgram->vertexShader);
			break;
		case GL_FRAGMENT_SHADER_ARB:
			// create fragment shader
			shaderProgram->fragmentShader = glCreateShaderObjectARB(GL_FRAGMENT_SHADER_ARB);
			glShaderSourceARB(shaderProgram->fragmentShader, 1, (const GLcharARB **)&buffer, 0);
			glCompileShaderARB(shaderProgram->fragmentShader);
			break;
		default:
			common->Printf("R_LoadGLSLShader: no type\n");
			return;
	}

	common->Printf("\n");
}

/*
=================
R_LinkGLSLShader

links the GLSL vertex and fragment shaders together to form a GLSL program
=================
*/
static bool R_LinkGLSLShader(shaderProgram_t *shaderProgram, bool needsAttributes)
{
	GLint linked;

	shaderProgram->program = glCreateProgramObjectARB();

	glAttachObjectARB(shaderProgram->program, shaderProgram->vertexShader);
	glAttachObjectARB(shaderProgram->program, shaderProgram->fragmentShader);

	if (needsAttributes) {
		glBindAttribLocationARB(shaderProgram->program, 8, "attr_TexCoord");
		glBindAttribLocationARB(shaderProgram->program, 9, "attr_Tangent");
		glBindAttribLocationARB(shaderProgram->program, 10, "attr_Bitangent");
		glBindAttribLocationARB(shaderProgram->program, 11, "attr_Normal");
	}

	glLinkProgramARB(shaderProgram->program);

	glGetObjectParameterivARB(shaderProgram->program, GL_OBJECT_LINK_STATUS_ARB, &linked);

	if (!linked) {
		common->Printf("R_LinkGLSLShader: program failed to link\n");
		return false;
	}

	return true;
}

/*
=================
R_ValidateGLSLProgram

makes sure GLSL program is valid
=================
*/
static bool R_ValidateGLSLProgram(shaderProgram_t *shaderProgram)
{
	GLint validProgram;

	glValidateProgramARB(shaderProgram->program);

	glGetObjectParameterivARB(shaderProgram->program, GL_OBJECT_VALIDATE_STATUS_ARB, &validProgram);

	if (!validProgram) {
		common->Printf("R_ValidateGLSLProgram: program invalid\n");
		return false;
	}

	return true;
}


static void RB_GLSL_GetUniformLocations(shaderProgram_t *shader)
{
	shader->localLightOrigin = glGetUniformLocationARB(shader->program, "u_lightOrigin");
	shader->localViewOrigin = glGetUniformLocationARB(shader->program, "u_viewOrigin");
	shader->lightProjectionS = glGetUniformLocationARB(shader->program, "u_lightProjectionS");
	shader->lightProjectionT = glGetUniformLocationARB(shader->program, "u_lightProjectionT");
	shader->lightProjectionQ = glGetUniformLocationARB(shader->program, "u_lightProjectionQ");
	shader->lightFalloff = glGetUniformLocationARB(shader->program, "u_lightFalloff");
	shader->bumpMatrixS = glGetUniformLocationARB(shader->program, "u_bumpMatrixS");
	shader->bumpMatrixT = glGetUniformLocationARB(shader->program, "u_bumpMatrixT");
	shader->diffuseMatrixS = glGetUniformLocationARB(shader->program, "u_diffuseMatrixS");
	shader->diffuseMatrixT = glGetUniformLocationARB(shader->program, "u_diffuseMatrixT");
	shader->specularMatrixS = glGetUniformLocationARB(shader->program, "u_specularMatrixS");
	shader->specularMatrixT = glGetUniformLocationARB(shader->program, "u_specularMatrixT");
	shader->colorModulate = glGetUniformLocationARB(shader->program, "u_colorModulate");
	shader->colorAdd = glGetUniformLocationARB(shader->program, "u_colorAdd");
	shader->diffuseColor = glGetUniformLocationARB(shader->program, "u_diffuseColor");
	shader->specularColor = glGetUniformLocationARB(shader->program, "u_specularColor");

	shader->u_bumpTexture = glGetUniformLocationARB(shader->program, "u_bumpTexture");
	shader->u_lightFalloffTexture = glGetUniformLocationARB(shader->program, "u_lightFalloffTexture");
	shader->u_lightProjectionTexture = glGetUniformLocationARB(shader->program, "u_lightProjectionTexture");
	shader->u_diffuseTexture = glGetUniformLocationARB(shader->program, "u_diffuseTexture");
	shader->u_specularTexture = glGetUniformLocationARB(shader->program, "u_specularTexture");
	shader->u_specularFalloffTexture = glGetUniformLocationARB(shader->program, "u_specularFalloffTexture");

	shader->modelMatrix = glGetUniformLocationARB(shader->program, "u_modelMatrix");

	shader->modelMatrix = glGetUniformLocationARB(shader->program, "u_modelMatrix");

	// set texture locations
	glUseProgramObjectARB(shader->program);
	glUniform1iARB(shader->u_bumpTexture, 0);
	glUniform1iARB(shader->u_lightFalloffTexture, 1);
	glUniform1iARB(shader->u_lightProjectionTexture, 2);
	glUniform1iARB(shader->u_diffuseTexture, 3);
	glUniform1iARB(shader->u_specularTexture, 4);
	glUniform1iARB(shader->u_specularFalloffTexture, 5);
	glUseProgramObjectARB(0);
}

static bool RB_GLSL_InitShaders(void)
{
	// load interation shaders
	R_LoadGLSLShader("interaction.vert", &interactionShader, GL_VERTEX_SHADER_ARB);
	R_LoadGLSLShader("interaction.frag", &interactionShader, GL_FRAGMENT_SHADER_ARB);

	if (!R_LinkGLSLShader(&interactionShader, true) && !R_ValidateGLSLProgram(&interactionShader)) {
		return false;
	} else {
		RB_GLSL_GetUniformLocations(&interactionShader);
	}

	// load stencil shadow extrusion shaders
	R_LoadGLSLShader("shadow.vert", &shadowShader, GL_VERTEX_SHADER_ARB);
	R_LoadGLSLShader("shadow.frag", &shadowShader, GL_FRAGMENT_SHADER_ARB);

	if (!R_LinkGLSLShader(&shadowShader, false) && !R_ValidateGLSLProgram(&shadowShader)) {
		return false;
	} else {
		shadowShader.localLightOrigin = glGetUniformLocationARB(shadowShader.program, "u_lightOrigin");
	}

	return true;
}

/*
==================
R_ReloadGLSLPrograms_f
==================
*/
void R_ReloadGLSLPrograms_f(const idCmdArgs &args)
{
	int		i;

	common->Printf("----- R_ReloadGLSLPrograms -----\n");

	if (!RB_GLSL_InitShaders()) {
		common->Printf("GLSL shaders failed to init.\n");
	}

	glConfig.allowGLSLPath = true;

	common->Printf("-------------------------------\n");
}

void R_GLSL_Init(void)
{
	glConfig.allowGLSLPath = false;

	common->Printf("---------- R_GLSL_Init ----------\n");

	if (!glConfig.GLSLAvailable) {
		common->Printf("Not available.\n");
		return;
	}

	common->Printf("Available.\n");

	common->Printf("---------------------------------\n");
}
