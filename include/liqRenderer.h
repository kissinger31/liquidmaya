/*
**
** The contents of this file are subject to the Mozilla Public License Version
** 1.1 (the "License"); you may not use this file except in compliance with
** the License.  You may obtain a copy of the License at
** http://www.mozilla.org/MPL/
**
** Software distributed under the License is distributed on an "AS IS" basis,
** WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
** for the specific language governing rights and limitations under the
** License.
**
** The Original Code is the Liquid Rendering Toolkit.
**
** The Initial Developer of the Original Code is Colin Doncaster. Portions
** created by Colin Doncaster are Copyright (C) 2002. All Rights Reserved.
**
** Contributor(s): Berj Bannayan.
**
**
** The RenderMan (R) Interface Procedures and Protocol are:
** Copyright 1988, 1989, Pixar 
** All Rights Reserved
**
**
** RenderMan (R) is a registered trademark of Pixar
*/

#ifndef liqRenderer_H
#define liqRenderer_H

#include <maya/MString.h>

class liqRenderer
{
public:
    enum e_renderer { REN_PRMAN, REN_ENTROPY, REN_AQSIS, REN_DELIGHT };
    enum e_capability { BLOBBIES,
			POINTS,
			EYESPLITS };

    enum e_requirement	{
			SWAPPED_UVS,	// transpose u & v direction on NURBS
			__PREF		// use __Pref instead of Pref
			};

    liqRenderer(e_renderer renderer, MString version)
	: m_renderer(renderer), m_version(version)
    {
	// nothing else needed
    }

    virtual ~liqRenderer()
    {
	// nothing else needed
    }

    e_renderer		getRenderer() const
			{ return m_renderer; }
    MString		getVersion() const
			{ return m_version; }

    virtual bool	supports(e_capability capability) const = 0;
    virtual bool	requires(e_requirement requirement) const = 0;

private:
    e_renderer	m_renderer;
    MString	m_version;
};

#endif