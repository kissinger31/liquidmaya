/*
**
** The contents of this file are subject to the Mozilla Public License Version 1.1 (the 
** "License"); you may not use this file except in compliance with the License. You may 
** obtain a copy of the License at http://www.mozilla.org/MPL/ 
** 
** Software distributed under the License is distributed on an "AS IS" basis, WITHOUT 
** WARRANTY OF ANY KIND, either express or implied. See the License for the specific 
** language governing rights and limitations under the License. 
**
** The Original Code is the Liquid Rendering Toolkit. 
** 
** The Initial Developer of the Original Code is Colin Doncaster. Portions created by 
** Colin Doncaster are Copyright (C) 2002. All Rights Reserved. 
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

/* liquid command to export a shader ball with the selected shader */

// Standard Headers
#include <math.h>
#include <assert.h>
#include <time.h>
#include <stdio.h>
#include <iostream.h>
#include <sys/types.h>
// Renderman Headers
extern "C" {
#include <ri.h>
#ifdef PRMAN
#include <slo.h>
#endif
}
extern int debugMode;
#ifdef _WIN32
#include <process.h>
#include <malloc.h>
#else
#include <unistd.h>
#include <stdlib.h>
#include <alloca.h>
#include <pthread.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#endif

// Maya's Headers
#include <maya/MFn.h>
#include <maya/MString.h>
#include <maya/MPxCommand.h>
#include <maya/MCommandResult.h>
#include <maya/MStringArray.h>
#include <maya/MArgList.h>
#include <maya/MSelectionList.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MFnDoubleArrayData.h>
#include <maya/MPlug.h>
#include <maya/MDoubleArray.h>

#include <liqShader.h>
#include <liqPreviewShader.h>
#include <liqMemory.h>
#include <liquid.h>
#include <liqGlobalHelpers.h>


    // Set default values
#if defined(PRMAN)
    const char * liqPreviewShader::m_default_previewer = "rman";
#elif defined(AQSIS)
    const char * liqPreviewShader::m_default_previewer = "aqsis";
#elif defined(ENTROPY)
    const char * liqPreviewShader::m_default_previewer = "entropy";
#elif defined(PIXIE)
    const char * liqPreviewShader::m_default_previewer = "rndr";
#else
    // Force error at compile time
      error - unknown renderer
#endif



void* liqPreviewShader::creator()
//
//  Description:
//      Create a new instance of the translator
//
{
    return new liqPreviewShader();
}

liqPreviewShader::~liqPreviewShader()
//
//  Description:
//      Class destructor
//
{
}

typedef struct liqPreviewShoptions
{
    const char *shaderNodeName;
    const char *displayDriver;
    const char *renderCommand;
    bool shortShaderName;
} liqPreviewShoptions; 

int liquidOutpuPreviewShader( const char *fileName, liqPreviewShoptions *options );

#ifndef _WIN32
void liquidNewPreview( liqPreviewShoptions *options )
{
    int val = 0;
    int ret;
    if( !options->shaderNodeName || !options->displayDriver || !options->renderCommand )
    {
    	cerr << "Invalid options for shader preview" << endl;
    	pthread_exit( ( void * )&val);
    }
    	
    fflush(NULL);
    // Open a pipe to a command
    FILE *fp = popen( options->renderCommand, "w");
    if( !fp )
    {
    	char str[256];
	sprintf( str, "Opening pipe to %s ", options->renderCommand );
    	perror( str );
    	pthread_exit( ( void * )&val);
    }
    val = fork();
    if( val == -1 ) // Parent with error
    {
    	perror( "Fork for rib output" );
    	pthread_exit( ( void * )&val);
    }
    else if( val == 0 ) // Child
    {
	int fd = fileno(fp); 
	// Redirect stdout to pipe
	// Warning : messages should be sent to sdterr until stdout is restored
	ret = dup2( fd, 1 );
	if( ret < 0 )
	{
    	    perror( "Pipe redirect failed " );
    	    pthread_exit( ( void * )&val);
       }
       // And output RIB stdout
	cout << "# Outputing " << options->shaderNodeName << endl;
	liquidOutpuPreviewShader( NULL, options );
	_exit(0);
    }
    cerr << "Waiting for process" << val << " to finish " << endl;
    ret = waitpid( val, NULL, 0 );
    cerr << "Waiting for " << options->renderCommand << " to finish" << endl;
    pclose( fp );   	// Wait until render finish
    val = 1;	// Set a "all is ok" returned value
    //cout << "Stdout is still open" << endl;
    LIQDEBUGPRINTF("-> Thread preview is done.\n" );
    pthread_exit( ( void * )&val);
}
#endif // ifndef _WIN32

MStatus	liqPreviewShader::doIt( const MArgList& args )
{
#if defined( PRMAN ) || defined( ENTROPY ) || defined( AQSIS ) || defined( PIXIE )
    MStatus status;
    int i;
    liqPreviewShoptions preview;
    preview.shortShaderName = false;
    MString renderCmd( liqPreviewShader::m_default_previewer );
    bool useIt = true;
    MString shaderNodeName;
    for ( i = 0; i < args.length(); i++ ) {
      if ( MString( "-sphere" ) == args.asString( i, &status ) )  {
	    } else if ( MString( "-box" ) == args.asString( i, &status ) )  {
	    } else if ( MString( "-plane" ) == args.asString( i, &status ) )  {
	    } else if ( MString( "-noIt" ) == args.asString( i, &status ) )  {
		    useIt = false;
	    } else if ( MString( "-shader" ) == args.asString( i, &status ) )  {
		    i++;
		    shaderNodeName = args.asString( i, &status );
	    } else if ( MString( "-renderer" ) == args.asString( i, &status ) )  {
		    i++;
		    renderCmd = args.asString( i, &status ).asChar();
	    } else if ( MString( "-sshn" ) == args.asString( i, &status ) )  {
		    preview.shortShaderName = true;
	    }
    }
    // Check values
    if( shaderNodeName == "" )
    {
    	cerr << "Need a shader name for previews" << endl;
    	return MS::kFailure; 
    }
    if( renderCmd == "" )
    {
    	cerr << "Need a render command for previews" << endl;
    	return MS::kFailure; 
    }
    preview.shaderNodeName = shaderNodeName.asChar();
    preview.renderCommand = renderCmd.asChar();
    preview.displayDriver = "framebuffer";
    
#ifdef _WIN32
    char *systemTempDirectory;
    systemTempDirectory = getenv("TEMP");
    MString tempRibName( systemTempDirectory );
    tempRibName += "/temp.rib";
    free( systemTempDirectory );
    systemTempDirectory = (char *)malloc( sizeof( char ) * 256 );
    strcpy( systemTempDirectory, tempRibName.asChar() );
    preview.displayDriver = "framebuffer";
    liquidOutpuPreviewShader( systemTempDirectory, &preview );
    _spawnlp(_P_DETACH, preview.renderCommand, preview.renderCommand, tempRibName.asChar(), NULL);
    free( systemTempDirectory );
#else
    LIQDEBUGPRINTF("-> Creating thread preview.\n" );
    pthread_t prevthread;
    if( pthread_create( & prevthread, NULL, (void *(*)(void *)) liquidNewPreview, ( void * ) &preview))
    {
    	perror( "Thread create" );
    	return MS::kFailure; 
    }
    void * threadreturn;
    // Wait for end of rendering thread
    // must do so to make sure local variables always exist for the renderer
    pthread_join( prevthread, &threadreturn  ); 
    LIQDEBUGPRINTF("-> End of thread preview.\n" );
#endif
    return MS::kSuccess; 
#else // PRMAN || ENTROPY || Aqsis
    return MS::kFailure; 
#endif
};
// Output preview RIB into fileName for a shader
// if fileName is RI_NULL : output to stdout
// return 1 on success
int liquidOutpuPreviewShader( const char *fileName, liqPreviewShoptions * options )
{
    MStatus status;
    if( fileName )
    	RiBegin( const_cast<char *>(fileName) );
    else
    	RiBegin( NULL );
    RiFrameBegin( 1 );
    RiFormat( 100, 100, 1 );
    RiDisplay( "liqPreviewShader", const_cast<char *>( options->displayDriver), "rgba", RI_NULL );
    RtFloat fov = 38;
    RiProjection( "perspective", "fov", &fov, RI_NULL );
    RiWorldBegin();
    RtLightHandle ambientLightH, directionalLightH;
    RtFloat intensity;
    intensity = 0.2f;
    ambientLightH = RiLightSource( "ambientlight", "intensity", &intensity, RI_NULL );
    intensity = 1.0f;
    RtPoint from;
    from[0] = -2.0; from[1] = -1.0; from[2] = 1.0;
    RtPoint to;
    to[0] = 0.0; to[1] = 0.0; to[2] = 0.0;
    directionalLightH = RiLightSource( "distantlight", "intensity", &intensity, "from", &from, "to", &to, RI_NULL );

    RiAttributeBegin();
    MSelectionList shaderNameList;
    MObject	shaderObj;
    shaderNameList.add( options->shaderNodeName );
    shaderNameList.getDependNode( 0, shaderObj );
    if( shaderObj == MObject::kNullObj )
    {
    	cerr << "Can't find node for " << options->shaderNodeName << endl;
	RiEnd();
    	return 0;
    }
    MFnDependencyNode assignedShader( shaderObj );
    liqShader currentShader( shaderObj );
    RtToken *tokenArray = (RtToken *)alloca( sizeof(RtToken) * currentShader.numTPV );
    RtPointer *pointerArray = (RtPointer *)alloca( sizeof(RtPointer) * currentShader.numTPV );

    assignTokenArrays( currentShader.numTPV, currentShader.tokenPointerArray, tokenArray, pointerArray );

    RiTranslate( 0, 0, 1.5 );	
	
    double displacementBounds = 0.0;
    assignedShader.findPlug( "displacementBound", &status ).getValue( displacementBounds );
    if ( displacementBounds > 0.0 ) {
    	RiAttribute( "bound", "displacement", &displacementBounds, RI_NULL );
    }
    char *shaderFileName;
    LIQ_GET_SHADER_FILE_NAME(shaderFileName, options->shortShaderName, currentShader );
	
    if ( currentShader.shader_type == SHADER_TYPE_SURFACE ) {
    	RiColor( currentShader.rmColor );
  	RiOpacity( currentShader.rmOpacity );
    	RiSurfaceV ( shaderFileName, currentShader.numTPV, tokenArray, pointerArray );
    } else if ( currentShader.shader_type == SHADER_TYPE_DISPLACEMENT ) {
    	RiDisplacementV ( shaderFileName, currentShader.numTPV, tokenArray, pointerArray );
    }

    RiSphere( 0.5, 0.0, 0.5, 360.0, RI_NULL );
	
    RiAttributeEnd();
    RiWorldEnd();
    RiFrameEnd();
    RiEnd();
    fflush( NULL );
    debugMode = 1;
    LIQDEBUGPRINTF("# Rib output done.\n" );
    debugMode = 0;
    return 1;
}

