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
*/

/* ______________________________________________________________________
**
** Liquid Rib Node Source
** ______________________________________________________________________
*/

// Standard Headers
#include <math.h>
#include <assert.h>
#include <time.h>
#include <stdio.h>
#include <sys/types.h>

#ifndef _WIN32
// Dynamic Object Headers
#include <dlfcn.h>
#endif

// Renderman Headers
extern "C" {
#include <ri.h>
}

#ifdef _WIN32
#include <process.h>
#include <malloc.h>
#else
#include <unistd.h>
#include <stdlib.h>
#include <alloca.h>
#endif

// Maya's Headers
#include <maya/MPlug.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MColor.h>
#include <maya/MFnPhongShader.h>
#include <maya/MFnBlinnShader.h>
#include <maya/MFnLambertShader.h>
#include <maya/MPlugArray.h>
#include <maya/MObjectArray.h>
#include <maya/MFnSet.h>
#include <maya/MGlobal.h>
#include <maya/MSelectionList.h>
#include <maya/MFnDagNode.h>
#include <maya/MBoundingBox.h>
#include <maya/MFnAttribute.h>
#include <maya/MFnParticleSystem.h>
#include <maya/MVectorArray.h>
#include <maya/MFnDoubleArrayData.h>

#include <liquid.h>
#include <liqGlobalHelpers.h>
#include <liqRibNode.h>
#include <liqMemory.h>

#ifdef _WIN32
#undef min
#undef max
#endif

extern int debugMode;

extern MStringArray liqglo_preReadArchive;
extern MStringArray liqglo_preRibBox;
extern MStringArray liqglo_preReadArchiveShadow;
extern MStringArray liqglo_preRibBoxShadow;

/**
 * Class constructor.
 */
liqRibNode::liqRibNode( liqRibNode * instanceOfNode,
                        const MString instanceOfNodeStr )
  :   next( NULL ),
      matXForm( MRX_Const ),
      bodyXForm( MRX_Const ),
      instance( instanceOfNode ),
      instanceStr( instanceOfNodeStr ),
      overrideColor( false )
{
  LIQDEBUGPRINTF( "-> creating rib node\n");
  for( unsigned i = 0; i < LIQMAXMOTIONSAMPLES; i++ )
    objects[ i ] = NULL;

  name.clear();
  isRibBox = false;
  isArchive = false;
  isDelayedArchive = false;
  matteMode = false;

  shading.shadingRate = 1.0f;
  shading.diceRasterOrient = true;

  trace.displacements = false;
  trace.sampleMotion = false;
  trace.bias = 0.01f;
  trace.maxDiffuseDepth = 1;
  trace.maxSpecularDepth = 2;

  visibility.camera = true;
  visibility.trace = false;
  visibility.photon = false;
  visibility.transmission = visibility::TRANSMISSION_TRANSPARENT;

  irradiance.shadingRate = 1.0f;
  irradiance.nSamples = 64;
  irradiance.maxError = 1.0f;
  irradiance.handle = "";
  irradiance.fileMode = irradiance::FILEMODE_NONE;

  photon.globalMap = "";
  photon.causticMap = "";
  photon.shadingModel = photon::SHADINGMODEL_MATTE;
  photon.estimator = 100;
  
  motion.transformationBlur = true;;
  motion.deformationBlur = true;
  motion.samples = 2;
  motion.factor = 1.0;
  
  invisible = false;
}

/**
 * Class destructor.
 */
liqRibNode::~liqRibNode()
{
  LIQDEBUGPRINTF( "-> killing rib node %s\n", name.asChar() );

  for( unsigned i = 0; i < LIQMAXMOTIONSAMPLES; i++ ) {
    if ( objects[ i ] != NULL ) {
      LIQDEBUGPRINTF( "-> killing %d. ref\n", i );
      objects[ i ]->unref();
      objects[ i ] = NULL;
    }
  }
  LIQDEBUGPRINTF( "-> killing no obj\n" );
  no = NULL;
  name.clear();
  ribBoxString.clear();
  archiveString.clear();
  delayedArchiveString.clear();
  irradiance.handle.clear();
  photon.globalMap.clear();
  photon.causticMap.clear();
  LIQDEBUGPRINTF( "-> finished killing rib node.\n" );
}

/**
 * Get the object referred to by this node.
 * This returns the surface, mesh, light, etc. this node points to.
 */
liqRibObj * liqRibNode::object( unsigned interval )
{
  return objects[ interval ];
}

/** 
 * Set this node with the given path.
 * If this node already refers to the given object, then it is assumed that the
 * path represents the object at the next frame.
 * This method also scans the dag upwards and thereby sets any attributes
 * Liquid knows that have non-default values and sets them for to this node.
 */
void liqRibNode::set( const MDagPath &path, int sample, ObjectType objType, int particleId )
{
  LIQDEBUGPRINTF( "-> setting rib node\n");
  DagPath = path;
#if 0
  int instanceNum = path.instanceNumber();
#endif
  isRibBox = false;
  isArchive = false;
  isDelayedArchive = false;
  MStatus status;
  MFnDagNode fnNode( path );
  MPlug nPlug;
  status.clear();

  MSelectionList hierarchy; // needed to find objectSets later below
  MDagPath dagSearcher( path );

  do { // while( dagSearcher.length() > 0 )
    dagSearcher.pop(); // Go upwards (should be a transform node)
    
    hierarchy.add( dagSearcher, MObject::kNullObj, true );

    if ( dagSearcher.apiType( &status ) == MFn::kTransform ) {
      MFnDagNode nodePeeker( dagSearcher );
            
      // misc. group ----------------------------------------------------------
  
      if ( !invisible ) {
        status.clear();
        nPlug = nodePeeker.findPlug( MString( "template" ), &status );
        if ( status == MS::kSuccess ) {
          nPlug.getValue( invisible );
          break; // Exit do..while loop -- IF OBJECT ATTRIBUTES NEED TO BE PARSED FOR INVISIBLE OBJECTS TOO IN THE FUTURE -- REMOVE THIS LINE!
        } else {
          status.clear();
          nPlug = nodePeeker.findPlug( MString( "liqInvisible" ), &status );
          if ( status == MS::kSuccess ) {
            nPlug.getValue( invisible );
            break; // Exit do..while loop -- IF OBJECT ATTRIBUTES NEED TO BE PARSED FOR INVISIBLE OBJECTS TOO IN THE FUTURE -- REMOVE THIS LINE!
          }
        }
      }      
  
      if ( shading.shadingRate == 1.0f ) {
        status.clear();
        nPlug = nodePeeker.findPlug( MString( "liqShadingRate" ), &status );
        if ( status == MS::kSuccess )
          nPlug.getValue( shading.shadingRate );
      }

      if ( shading.diceRasterOrient == true ) {
        status.clear();
        nPlug = nodePeeker.findPlug( MString( "liqDiceRasterOrient" ), &status );
        if ( status == MS::kSuccess )
          nPlug.getValue( shading.diceRasterOrient );
      }

      // trace group ----------------------------------------------------------
      if ( trace.sampleMotion == false ) {
        status.clear();
        nPlug = nodePeeker.findPlug( MString( "liqTraceSampleMotion" ), &status );
        if ( status == MS::kSuccess )
          nPlug.getValue( trace.sampleMotion );
      }

      if ( trace.displacements == false ) {
        status.clear();
        nPlug = nodePeeker.findPlug( MString( "liqTraceDisplacements" ), &status );
        if ( status == MS::kSuccess )
          nPlug.getValue( trace.displacements );
      }

      if ( trace.bias == 0.01f ) {
        status.clear();
        nPlug = nodePeeker.findPlug( MString( "liqTraceBias" ), &status );
        if ( status == MS::kSuccess )
          nPlug.getValue( trace.bias );
      }

      if ( trace.maxDiffuseDepth == 1 ) {
        status.clear();
        nPlug = nodePeeker.findPlug( MString( "liqMaxDiffuseDepth" ), &status );
        if ( status == MS::kSuccess )
          nPlug.getValue( trace.maxDiffuseDepth );
      }

      if ( trace.maxSpecularDepth == 2 ) {
        status.clear();
        nPlug = nodePeeker.findPlug( MString( "liqMaxSpecularDepth" ), &status );
        if ( status == MS::kSuccess )
          nPlug.getValue( trace.maxSpecularDepth );
      }

      // visibility group -----------------------------------------------------

      if ( visibility.camera == true ) {
        status.clear();
        nPlug = nodePeeker.findPlug( MString( "liqVisibilityCamera" ), &status );
        if ( status == MS::kSuccess )
          nPlug.getValue( visibility.camera );
      }

      if ( visibility.trace == false ) {
        status.clear();
        nPlug = nodePeeker.findPlug( MString( "liqVisibilityTrace" ), &status );
        if ( status == MS::kSuccess )
          nPlug.getValue( visibility.trace );
      }

      if ( visibility.transmission == visibility::TRANSMISSION_TRANSPARENT ) {
        status.clear();
        nPlug = nodePeeker.findPlug( MString( "liqVisibilityTransmission" ), &status );
        if ( status == MS::kSuccess )
          nPlug.getValue( ( int& ) visibility.transmission );
      }

      // irradiance group -----------------------------------------------------

      if ( irradiance.shadingRate == 1.0f ) {
        status.clear();
        nPlug = nodePeeker.findPlug( MString( "liqIrradianceShadingRate" ), &status );
        if ( status == MS::kSuccess )
          nPlug.getValue( irradiance.shadingRate );
      }

      if ( irradiance.nSamples == 64 ) {
        status.clear();
        nPlug = nodePeeker.findPlug( MString( "liqIrradianceNSamples" ), &status );
        if ( status == MS::kSuccess )
          nPlug.getValue( irradiance.nSamples );
      }

      if ( irradiance.maxError == 1.0f ) {
        status.clear();
        nPlug = nodePeeker.findPlug( MString( "liqIrradianceMaxError;" ), &status );
        if ( status == MS::kSuccess )
          nPlug.getValue( irradiance.maxError );
      }

      if ( irradiance.handle == "" ) {
        status.clear();
        nPlug = nodePeeker.findPlug( MString( "liqIrradianceHandle" ), &status );
        if ( status == MS::kSuccess )
          nPlug.getValue( irradiance.handle );
      }

      if ( irradiance.fileMode == irradiance::FILEMODE_NONE ) {
        status.clear();
        nPlug = nodePeeker.findPlug( MString( "liqIrradianceFileMode" ), &status );
        if ( status == MS::kSuccess )
          nPlug.getValue( ( int& ) irradiance.fileMode );
      }

      // photon group ---------------------------------------------------------

      if ( photon.globalMap == "" ) {
        status.clear();
        nPlug = nodePeeker.findPlug( MString( "liqPhotonGlobalMap" ), &status );
        if ( status == MS::kSuccess )
          nPlug.getValue( photon.globalMap );
      }

      if ( photon.causticMap == "" ) {
        status.clear();
        nPlug = nodePeeker.findPlug( MString( "liqPhotonCausicMap" ), &status );
        if ( status == MS::kSuccess )
          nPlug.getValue( photon.causticMap );
      }

      if ( photon.shadingModel == photon::SHADINGMODEL_MATTE ) {
        status.clear();
        nPlug = nodePeeker.findPlug( MString( "liqPhotonShadingModel" ), &status );
        if ( status == MS::kSuccess )
          nPlug.getValue( ( int& ) photon.shadingModel );
      }

      if ( photon.estimator == 100 ) {
        status.clear();
        nPlug = nodePeeker.findPlug( MString( "liqPhotonEstimator" ), &status );
        if ( status == MS::kSuccess )
          nPlug.getValue( photon.estimator );
      }

      /*MFnDependencyNode nodeFn( nodePeeker );
      MStringArray floatAttributesFound  = findAttributesByPrefix( "rmanF", nodeFn );
      MStringArray pointAttributesFound  = findAttributesByPrefix( "rmanP", nodeFn );
      MStringArray vectorAttributesFound = findAttributesByPrefix( "rmanV", nodeFn );
      MStringArray normalAttributesFound = findAttributesByPrefix( "rmanN", nodeFn );
      MStringArray colorAttributesFound  = findAttributesByPrefix( "rmanC", nodeFn );
      MStringArray stringAttributesFound = findAttributesByPrefix( "rmanS", nodeFn );*/
    }
  }
  while( dagSearcher.length() > 0 );

  // Set membership handling
  //
  if ( grouping.membership == "" ) {
    MObjectArray setArray;
    MGlobal::getAssociatedSets( hierarchy, setArray );

    for( unsigned i = 0; i < setArray.length(); i++ ) {
      MFnDependencyNode depNodeFn( setArray[ i ] );
      status.clear();
      MPlug plug = depNodeFn.findPlug( "liqTraceSet", &status );
      if ( status == MS::kSuccess ) {
        bool value = false;
        plug.getValue( value );
        if ( value ) {
          status.clear();
          grouping.membership += " +" + depNodeFn.name( &status );
        }
      }
    }

    status.clear();
    grouping.membership = path.fullPathName( &status ) + grouping.membership;
  }

  /*
  nPlug = fnNode.findPlug( MString( "transformationBlur" ), &status );
  if ( status == MS::kSuccess ) {
    nPlug.getValue( doMotion );
  }
  status.clear();
  nPlug = fnNode.findPlug( MString( "deformationBlur" ), &status );
  if ( status == MS::kSuccess ) {
    nPlug.getValue( doDef );
  }*/
  status.clear();
  nPlug = fnNode.findPlug( MString( "ribBox" ), &status );
  if ( status == MS::kSuccess ) {
    MString ribBoxValue;
    nPlug.getValue( ribBoxValue );
    if ( ribBoxValue.substring(0,2) == "*H*" ) {
      MString parseThis = ribBoxValue.substring(3, ribBoxValue.length() - 1 );
      liqglo_preRibBox.append( parseString( parseThis ) );
    } else if ( ribBoxValue.substring(0,3) == "*SH*" ) {
      MString parseThis = ribBoxValue.substring(3, ribBoxValue.length() - 1 );
      liqglo_preRibBoxShadow.append( parseString( parseThis ) );
    } else {
      ribBoxString = parseString( ribBoxValue );
      isRibBox = true;
    }
  }
  status.clear();
  nPlug = fnNode.findPlug( MString( "ribArchive" ), &status );
  if ( status == MS::kSuccess ) {
    MString archiveValue;
    nPlug.getValue( archiveValue );
    if ( archiveValue.substring(0,2) == "*H*" ) {
      MString parseThis = archiveValue.substring(3, archiveValue.length() - 1 );
      liqglo_preReadArchive.append( parseString( parseThis ) );
    } else if ( archiveValue.substring(0,3) == "*SH*" ) {
      MString parseThis = archiveValue.substring(3, archiveValue.length() - 1 );
      liqglo_preReadArchiveShadow.append( parseString( parseThis ) );
    } else {
      archiveString = parseString( archiveValue );
      isArchive = true;
    }
  }
  status.clear();
  nPlug = fnNode.findPlug( MString( "ribDelayedArchive" ), &status );
  if ( status == MS::kSuccess ) {
    MString delayedArchiveValue;
    nPlug.getValue( delayedArchiveValue );
    delayedArchiveString = parseString( delayedArchiveValue );
    isDelayedArchive = true;

    MStatus Dstatus;
    MPlug delayedPlug = fnNode.findPlug( MString( "ribDelayedArchiveBBox" ), &Dstatus );
    if ( ( Dstatus == MS::kSuccess ) && ( delayedPlug.isConnected() ) ) {
      MPlugArray delayedNodeArray;
      delayedPlug.connectedTo( delayedNodeArray, true, true );
      MObject delayedNodeObj;
      delayedNodeObj = delayedNodeArray[0].node();
      MFnDagNode delayedfnNode( delayedNodeObj );

      MBoundingBox bounding = delayedfnNode.boundingBox();
      MPoint bMin = bounding.min();
      MPoint bMax = bounding.max();
      bound[0] = bMin.x;
      bound[1] = bMin.y;
      bound[2] = bMin.z;
      bound[3] = bMax.x;
      bound[4] = bMax.y;
      bound[5] = bMax.z;
    }
  }



  // Get the object's color
  //
  if ( objType != MRT_Shader ) {
    MObject shadingGroup = findShadingGroup( path );
    if ( shadingGroup != MObject::kNullObj ) {
      assignedShadingGroup.setObject( shadingGroup );
      MObject surfaceShader = findShader( shadingGroup );
      assignedShader.setObject( surfaceShader );
      assignedDisp.setObject( findDisp( shadingGroup ) );
      assignedVolume.setObject( findVolume( shadingGroup ) );
      if ( ( surfaceShader == MObject::kNullObj ) || !getColor( surfaceShader, color ) ) {
        // This is how we specify that the color was not found.
        //
        color.r = -1.0;
      }
      matteMode = getMatteMode( surfaceShader );
    } else {
      color.r = -1.0;
    }
    doubleSided = isObjectTwoSided( path );
  }

  // Check to see if the object should have its color overridden
  // (if possible).
  //
  nPlug = fnNode.findPlug( MString( "useParticleColorWhenInstanced" ), &status );
  if ( status == MS::kSuccess )
  {
    bool override;
    nPlug.getValue( override );
    if ( override && particleId != -1 )
    {
      // Traverse upwards, looking for some connection between this
      // geometry hierarchy and a particle instancer node.
      //
      MFnDagNode dagNode( path.node() );
      bool foundInstancerNode = false;
      while (1)
      {
        // The instancer is always connected to the "matrix" attribute.
        //
        MPlug matrixPlug = dagNode.findPlug( MString( "matrix" ), &status );
        if ( status != MS::kSuccess )
        {
          break;
        }

        // If the matrix plug is connected, iterate over the connections
        // to see if one of them is an instancer.
        //
        if ( matrixPlug.isConnected() )
        {
          MPlugArray connections;
          matrixPlug.connectedTo( connections, false, true );
          for ( int i = 0; i < connections.length(); i++ )
          {
            MObject obj = connections[i].node();
            if ( obj.hasFn( MFn::kInstancer ) )
            {
              dagNode.setObject( obj );
              foundInstancerNode = true;
              break;
            }

          }
        }

        // If we've found an instancer or we're at the top of the
        // hierarchy, break.
        //
        if ( foundInstancerNode || dagNode.parentCount() == 0 )
        {
          break;
        }
        dagNode.setObject( dagNode.parent( 0 ) );
      }

      // If we've got an instancer, find the associated particle system.
      //
      if ( foundInstancerNode )
      {
        // Find out what particles we're replacing.
        //
        MPlug inputPointsPlug = dagNode.findPlug( "inputPoints", &status );
        if ( status == MS::kSuccess )
        {
          // Find the array of connected plugs.
          //
          MPlugArray sourcePlugArray;
          inputPointsPlug.connectedTo( sourcePlugArray, true, false, &status );

          // There SHOULD always be a connected plug,
          // but this is a safety check.
          //
          if ( sourcePlugArray.length() > 0 )
          {
            MObject sourceObject = sourcePlugArray[0].node();

            // Another sanity check: make sure the source is
            // actually a particle system.
            //
            if ( sourceObject.hasFn( MFn::kParticle ) )
            {
              MFnParticleSystem particles( sourceObject );

              // Proceed with color overrides if the rgbPP exists.
              //
              if ( particles.hasRgb() )
              {
                MVectorArray rgbPP;
                particles.rgb( rgbPP );

                // Find the ID's
                //
                MPlug idPlug = particles.findPlug( "id", &status );
                if ( status == MS::kSuccess )
                {
                  MObject idObject;
                  idPlug.getValue( idObject );
                  MFnDoubleArrayData idArray( idObject, &status );

                  // Look for an id that matches.
                  //
                  for ( int i = 0; i < idArray.length(); i++ )
                  {
                    // If a match is found, grab the color.
                    //
                    if ( static_cast<int>(idArray[i]) == particleId )
                    {
                      color.r = rgbPP[i].x;
                      color.g = rgbPP[i].y;
                      color.b = rgbPP[i].z;
                      overrideColor = true;
                      break;
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }

  // Create a new RIB object for the given path
  //
  LIQDEBUGPRINTF( "-> creating rib object for given path\n");

  MObject obj = path.node();
  no = new liqRibObj( path, objType );
  LIQDEBUGPRINTF( "-> creating rib object for reference\n");
  no->ref();

  LIQDEBUGPRINTF( "-> getting objects name\n");
  name = path.fullPathName();

  if ( name == "" ) {
    LIQDEBUGPRINTF( "-> name unknown -- searching for name\n");
    MDagPath parentPath = path;
    parentPath.pop();

    name = parentPath.fullPathName( &status );
    LIQDEBUGPRINTF( "-> found name\n");
  }
  
  if ( objType == MRT_RibGen ) {
    name += "RIBGEN";
  }

  LIQDEBUGPRINTF( "-> inserting object into ribnode's obj sample table\n" );
  if ( objects[ sample ] == NULL ) {
    objects[ sample ] = no;
  }
  
  LIQDEBUGPRINTF( "-> done creating rib object for given path\n");
}


/**
 * Return the path in the DAG to the instance that this node represents.
 */
MDagPath & liqRibNode::path()
{
  return DagPath;
}

/**
 * Find the shading group assigned to the given object.
 */
MObject liqRibNode::findShadingGroup( const MDagPath& path )
{
  LIQDEBUGPRINTF( "-> finding rib node shading group\n");
  MSelectionList objects;
  objects.add( path );
  MObjectArray setArray;

  // Get all of the sets that this object belongs to
  //
  MGlobal::getAssociatedSets( objects, setArray );
  MObject mobj;

  // Look for a set that is a "shading group"
  //
  for ( int i=0; i<setArray.length(); i++ )
  {
    mobj = setArray[i];
    MFnSet fnSet( mobj );
    MStatus stat;
    if ( MFnSet::kRenderableOnly == fnSet.restriction(&stat) )
    {
      return mobj;
    }
  }
  return MObject::kNullObj;
}

/**
 * Find the shading node for the given shading group.
 */
MObject liqRibNode::findShader( MObject& group )
{
  LIQDEBUGPRINTF( "-> finding shader for rib node shading group\n");
  MFnDependencyNode fnNode( group );
  MPlug shaderPlug = fnNode.findPlug( "surfaceShader" );

  if ( !shaderPlug.isNull() ) {
    MPlugArray connectedPlugs;
    bool asSrc = false;
    bool asDst = true;
    shaderPlug.connectedTo( connectedPlugs, asDst, asSrc );
    if ( connectedPlugs.length() != 1 ) {
      //cerr << "Error getting shader\n";
    }
    else
      return connectedPlugs[0].node();
  }

  return MObject::kNullObj;
}

/**
 * Find the displacement node for the given shading group
 */
MObject liqRibNode::findDisp( MObject& group )
{
  LIQDEBUGPRINTF( "-> finding shader for rib node shading group\n");
  MFnDependencyNode fnNode( group );
  MPlug shaderPlug = fnNode.findPlug( "displacementShader" );

  if ( !shaderPlug.isNull() ) {
    MPlugArray connectedPlugs;
    bool asSrc = false;
    bool asDst = true;
    shaderPlug.connectedTo( connectedPlugs, asDst, asSrc );
    if ( connectedPlugs.length() != 1 ) {
      //cerr << "Error getting shader\n";
    }
    else
      return connectedPlugs[0].node();
  }

  return MObject::kNullObj;
}

/**
 * Find the volume shading node for the given shading group.
 */
MObject liqRibNode::findVolume( MObject& group )
{
  LIQDEBUGPRINTF( "-> finding shader for rib node shading group\n");
  MFnDependencyNode fnNode( group );
  MPlug shaderPlug = fnNode.findPlug( "volumeShader" );

  if ( !shaderPlug.isNull() ) {
    MPlugArray connectedPlugs;
    bool asSrc = false;
    bool asDst = true;
    shaderPlug.connectedTo( connectedPlugs, asDst, asSrc );
    if ( connectedPlugs.length() != 1 ) {
      //cerr << "Error getting shader\n";
    }
    else
      return connectedPlugs[0].node();
  }

  return MObject::kNullObj;
}

/**
 * Get the list of all ignored lights for the given shading group.
 */
void liqRibNode::getIgnoredLights( MObject& group, MObjectArray& ignoredLights )
{
  LIQDEBUGPRINTF( "-> getting ignored lights\n");
  MFnDependencyNode fnNode( group );
  MPlug ilPlug = fnNode.findPlug( "ignoredLights" );

  if ( !ilPlug.isNull() ) {
    MPlugArray connectedPlugs;
    bool asSrc = false;
    bool asDst = true;

    // The ignoredLights attribute is an array so this should
    // never happen
    //
    if ( !ilPlug.isArray() )
      return;

    for ( unsigned i=0; i<ilPlug.numConnectedElements(); i++ )
    {
      MPlug elemPlug = ilPlug.elementByPhysicalIndex( i );
      connectedPlugs.clear();
      elemPlug.connectedTo( connectedPlugs, asDst, asSrc );

      // Since elemPlug is a destination there should
      // only be 1 connection to it
      //
      ignoredLights.append( connectedPlugs[0].node() );
    }
  }
}

/**
 * Get the list of all ignored lights for the given for this node.
 */
void liqRibNode::getIgnoredLights( MObjectArray& ignoredLights )
{
  MStatus status;
  LIQDEBUGPRINTF( "-> getting ignored lights\n");
  MFnDependencyNode fnNode( DagPath.node() );
  MPlug msgPlug = fnNode.findPlug( "message", &status );

  if ( status != MS::kSuccess ) return;

  MPlugArray llPlugs;
  msgPlug.connectedTo(llPlugs, true, true);

  for ( unsigned i=0; i<llPlugs.length(); i++ )
  {
    MPlug llPlug = llPlugs[i];
    MObject llPlugAttr = llPlug.attribute();
    MFnAttribute llPlugAttrFn(llPlugAttr);

    if (llPlugAttrFn.name() == MString( "objectIgnored" ))
    {
      MPlug llParentPlug = llPlug.parent(&status);
      int numChildren  = llParentPlug.numChildren();

      for (int k=0; k<numChildren; k++)
      {
        MPlug   childPlug  = llParentPlug.child(k);
        MObject llChildAttr = childPlug.attribute();
        MFnAttribute llChildAttrFn(llChildAttr);

        if (llChildAttrFn.name() == MString( "lightIgnored" ))
        {
          MPlugArray connectedPlugs;
          childPlug.connectedTo(connectedPlugs,true,true);
          if ( connectedPlugs[0].node().hasFn( MFn::kSet ) ) {
            MStatus setStatus;
            MFnDependencyNode listSetNode( connectedPlugs[0].node() );
            MPlug setPlug = fnNode.findPlug( "dagSetMembers", &setStatus );
            if ( setStatus == MS::kSuccess ) {
              MPlugArray setConnectedPlugs;
              setPlug.connectedTo(setConnectedPlugs,true,true);
              ignoredLights.append( setConnectedPlugs[0].node() );
            }
          } else {
            ignoredLights.append( connectedPlugs[0].node() );
          }
        }
      }
    }
  }
}

/**
 * Get the color of the given shading node.
 */
bool liqRibNode::getColor( MObject& shader, MColor& color )
{
  LIQDEBUGPRINTF( "-> getting a shader color\n");
  switch ( shader.apiType() )
  {
  case MFn::kLambert :
  {
    MFnLambertShader fnShader( shader );
    color = fnShader.color();
    break;
  }
  case MFn::kBlinn :
  {
    MFnBlinnShader fnShader( shader );
    color = fnShader.color();
    break;
  }
  case MFn::kPhong :
  {
    MFnPhongShader fnShader( shader );
    color = fnShader.color();
    break;
  }
  default:
  {
    MFnDependencyNode fnNode( shader );
    MPlug colorPlug = fnNode.findPlug( "outColor" );
    colorPlug[0].getValue( color.r );
    colorPlug[1].getValue( color.g );
    colorPlug[2].getValue( color.b );
    return false;
  }
  }
  return true;
}

/**
 * Check to see if we should make this a matte object.
 */
bool liqRibNode::getMatteMode( MObject& shader )
{
  MObject matteModeObj;
  short matteModeInt;
  MStatus myStatus;
  LIQDEBUGPRINTF( "-> getting matte mode\n");
  if ( !shader.isNull() ) {
    MFnDependencyNode fnNode( shader );
    MPlug mattePlug = fnNode.findPlug( "matteOpacityMode", &myStatus );
    if ( myStatus == MS::kSuccess ) {
      mattePlug.getValue( matteModeInt );
      LIQDEBUGPRINTF(  "-> matte mode: %d \n", matteModeInt );
      if ( matteModeInt == 0 ) {
        return true;
      }
    }
  }
  return false;
}

/**
 * Checks if this node actually points to any objects.
 */
bool liqRibNode::hasNObjects( unsigned n )
{
  for( int i = 0; i < n; i++ ) {
    if( objects[i] == NULL ) {
      return false;
    }
  }
  return true;
}
