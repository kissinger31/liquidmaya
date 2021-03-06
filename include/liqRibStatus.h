/*
**
** The contents of this file are subject to the Mozilla Public License Version
** 1.1 (the "License"); you may not use this file except in compliance with
** the License. You may obtain a copy of the License at
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
**
*/

#ifndef _H_liqRibStatus
#define _H_liqRibStatus
/* ______________________________________________________________________
** 
** Liquid RIBStatus Header File
** ______________________________________________________________________
*/

#include <ri.h>
#include <maya/MDagPath.h>
//#include <zlib.h>

class liqRibStatus {
public:
				typedef enum
		{
		  rpFinal,
		  rpShadow,
		  rpReflection,
		  rpEnvironment,
		  rpTraverseOnly,
		  rpDepth,
		  rpReference
		} RenderingPass;
		
		typedef enum
		{
		  reInfo = RIE_INFO,
		  reWarning = RIE_WARNING,
		  reError = RIE_ERROR,
		  reSevere = RIE_SEVERE
		} RenderingError;
			
		FILE *ribFP;
		int		frame;
		RenderingPass renderPass;		
		bool	transBlur, defBlur;
		bool  compressed, binary;
		char *objectName;
		RtMatrix	cameraMatrix;
		RtFloat shutterAngle;
		MDagPath dagPath;
		RtFloat *sampleTimes;
		long	motionSamples;
		void * RiConnection;
		
		liqRibStatus() {};
		virtual     	  ~liqRibStatus() {};
		
		//virtual void      ReportError( RenderingError e, const char *fmt, ... ) = 0;
};

#endif
