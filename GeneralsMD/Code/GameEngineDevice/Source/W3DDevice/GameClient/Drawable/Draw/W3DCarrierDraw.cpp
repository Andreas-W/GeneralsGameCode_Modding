// FILE: W3DCarrierDraw.cpp ////////////////////////////////////////////////////////////////////////////

// Desc: Render stationed Objects with carriers
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "Common/Xfer.h"
#include "GameClient/Drawable.h"
#include "GameLogic/Object.h"
#include "GameLogic/Module/ContainModule.h"
#include "W3DDevice/GameClient/Module/W3DCarrierDraw.h"

//-------------------------------------------------------------------------------------------------
W3DCarrierDrawModuleData::W3DCarrierDrawModuleData()
{
	m_dockingbone.clear();
}

//-------------------------------------------------------------------------------------------------
W3DCarrierDrawModuleData::~W3DCarrierDrawModuleData()
{
}

//-------------------------------------------------------------------------------------------------
void W3DCarrierDrawModuleData::buildFieldParse(MultiIniFieldParse& p)
{
	W3DModelDrawModuleData::buildFieldParse(p);

	static const FieldParse dataFieldParse[] =
	{
		{ "DockingBone",	INI::parseAsciiString,		NULL, offsetof(W3DCarrierDrawModuleData, m_dockingbone) },
		{ 0, 0, 0, 0 }
	};
	p.add(dataFieldParse);
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
W3DCarrierDraw::W3DCarrierDraw(Thing* thing, const ModuleData* moduleData)
	: W3DModelDraw(thing, moduleData)
{
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
W3DCarrierDraw::~W3DCarrierDraw()
{
}

//-------------------------------------------------------------------------------------------------
void W3DCarrierDraw::doDrawModule(const Matrix3D* transformMtx)
{
	W3DModelDraw::doDrawModule(transformMtx);

	Object* me = getDrawable()->getObject();
	if (me && me->getContain()) {

		const ContainedItemsList* docked_units = me->getContain()->getContainedItemsList();
		for (ContainedItemsList::const_iterator it = docked_units->begin(); it != docked_units->end(); ++it) {
			Drawable* riderDraw = (*it)->getDrawable();
			riderDraw->setColorTintEnvelope(*getDrawable()->getColorTintEnvelope());
			riderDraw->notifyDrawableDependencyCleared();
			riderDraw->draw(NULL);
		}
	}
}

//-------------------------------------------------------------------------------------------------
void W3DCarrierDraw::setHidden(Bool h)
{
	W3DModelDraw::setHidden(h);

	//TODO
	// 
	// We need to hide our rider, since he won't realize he's being contained in a contained container
	//Object* me = getDrawable()->getObject();
	//if (me && me->getContain()) {
		
	//}
}

//-------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void W3DCarrierDraw::crc(Xfer* xfer)
{

	// extend base class
	W3DModelDraw::crc(xfer);

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
	// ------------------------------------------------------------------------------------------------
void W3DCarrierDraw::xfer(Xfer* xfer)
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion(&version, currentVersion);

	// extend base class
	W3DModelDraw::xfer(xfer);

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void W3DCarrierDraw::loadPostProcess(void)
{

	// extend base class
	W3DModelDraw::loadPostProcess();

}  // end loadPostProcess
