#include "Animation_RuntimeGraphNode_Passthrough.h"

//-------------------------------------------------------------------------

namespace EE::Animation
{
    void PassthroughNode::Definition::InstantiateNode( InstantiationContext const& context, InstantiationOptions options ) const
    {
        EE_ASSERT( options == InstantiationOptions::NodeAlreadyCreated );
        auto pNode = reinterpret_cast<PassthroughNode*>( context.m_nodePtrs[m_nodeIdx] );
        context.SetNodePtrFromIndex( m_childNodeIdx, pNode->m_pChildNode );
    }

    //-------------------------------------------------------------------------

    SyncTrack const& PassthroughNode::GetSyncTrack() const
    {
        if ( IsValid() )
        {
            return m_pChildNode->GetSyncTrack();
        }
        else
        {
            return SyncTrack::s_defaultTrack;
        }
    }

    void PassthroughNode::InitializeInternal( GraphContext& context, SyncTrackTime const& initialTime )
    {
        EE_ASSERT( context.IsValid() );
        EE_ASSERT( m_pChildNode != nullptr );
        PoseNode::InitializeInternal( context, initialTime );

        //-------------------------------------------------------------------------

        m_pChildNode->Initialize( context, initialTime );

        //-------------------------------------------------------------------------

        if ( m_pChildNode->IsValid() )
        {
            m_duration = m_pChildNode->GetDuration();
            m_previousTime = m_pChildNode->GetPreviousTime();
            m_currentTime = m_pChildNode->GetCurrentTime();
        }
        else
        {
            m_previousTime = m_currentTime = 0.0f;
            m_duration = 0;
        }
    }

    void PassthroughNode::ShutdownInternal( GraphContext& context )
    {
        m_pChildNode->Shutdown( context );
        PoseNode::ShutdownInternal( context );
    }

    GraphPoseNodeResult PassthroughNode::Update( GraphContext& context, SyncTrackTimeRange const* pUpdateRange )
    {
        EE_ASSERT( context.IsValid() );
        MarkNodeActive( context );

        GraphPoseNodeResult result;
        result.m_sampledEventRange = context.GetEmptySampledEventRange();
        result.m_taskIdx = InvalidIndex;

        // Forward child node results
        if ( IsValid() )
        {
            result = m_pChildNode->Update( context, pUpdateRange );
            m_duration = m_pChildNode->GetDuration();
            m_previousTime = m_pChildNode->GetPreviousTime();
            m_currentTime = m_pChildNode->GetCurrentTime();
        }

        return result;
    }
}