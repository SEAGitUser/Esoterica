#pragma once
#include "Animation_ToolsGraphNode.h"
#include "Engine/Animation/Graph/Nodes/Animation_RuntimeGraphNode_IDs.h"
#include "Engine/Animation/Graph/Nodes/Animation_RuntimeGraphNode_Events.h"

//-------------------------------------------------------------------------

namespace EE::Animation
{
    enum class EventPriorityRule : uint8_t
    {
        EE_REFLECT_ENUM

        HighestWeight, // Prefer events that have a higher weight (if there are multiple events with the same weight the latest sampled will be chosen)
        HighestPercentageThrough, // Prefer events that have a higher percentage through (if there are multiple events with the same percentage through the latest sampled will be chosen)
    };

    enum class EventConditionOperator : uint8_t
    {
        EE_REFLECT_ENUM

        Or = 0,
        And,
    };

    //-------------------------------------------------------------------------

    class IDEventConditionToolsNode final : public FlowToolsNode
    {
        EE_REFLECT_TYPE( IDEventConditionToolsNode );

        enum class SearchRule : uint8_t
        {
            EE_REFLECT_ENUM

            SearchAll = 0,
            OnlySearchGraphEvents,
            OnlySearchAnimEvents,
        };

    public:

        IDEventConditionToolsNode();

        virtual char const* GetTypeName() const override { return "ID Event Condition"; }
        virtual char const* GetCategory() const override { return "Events"; }
        virtual TBitFlags<GraphType> GetAllowedParentGraphTypes() const override { return TBitFlags<GraphType>( GraphType::TransitionConduit, GraphType::ValueTree ); }
        virtual int16_t Compile( GraphCompilationContext& context ) const override;
        virtual void DrawInfoText( NodeGraph::DrawContext const& ctx, NodeGraph::UserContext* pUserContext ) override;
        virtual void GetLogicAndEventIDs( TVector<StringID>& outIDs ) const override;
        virtual void RenameLogicAndEventIDs( StringID oldID, StringID newID ) override;

    private:

        EE_REFLECT();
        EventConditionOperator                          m_operator = EventConditionOperator::Or;

        EE_REFLECT();
        SearchRule                                      m_searchRule = SearchRule::SearchAll;

        // When used in a transition, should we limit the search to only the source state? This is useful to ignore events from lower layers!
        EE_REFLECT( Category = "Advanced Search Rules" );
        bool                                            m_limitSearchToSourceState = false;

        // Ignore any events from states that we are transitioning away from
        EE_REFLECT( Category = "Advanced Search Rules" );
        bool                                            m_ignoreInactiveBranchEvents = false;

        EE_REFLECT( Category = "Conditions", CustomEditor = "AnimGraph_ID");
        TVector<StringID>                               m_eventIDs;
    };

    //-------------------------------------------------------------------------

    class IDEventToolsNode final : public FlowToolsNode
    {
        EE_REFLECT_TYPE( IDEventToolsNode );

    public:

        IDEventToolsNode();

        virtual char const* GetTypeName() const override { return "ID Event Value"; }
        virtual char const* GetCategory() const override { return "Events"; }
        virtual TBitFlags<GraphType> GetAllowedParentGraphTypes() const override { return TBitFlags<GraphType>( GraphType::TransitionConduit, GraphType::ValueTree ); }
        virtual int16_t Compile( GraphCompilationContext& context ) const override;
        virtual void DrawInfoText( NodeGraph::DrawContext const& ctx, NodeGraph::UserContext* pUserContext ) override;

    private:

        // The ID this node returns when nothing matches
        EE_REFLECT();
        StringID                                        m_defaultValue;

        // When used in a transition, should we limit the search to only the source state? This is useful to ignore events from lower layers!
        EE_REFLECT( Category = "Advanced Search Rules" );
        bool                                            m_limitSearchToSourceState = false;

        EE_REFLECT( Category = "Advanced Search Rules" );
        EventPriorityRule                               m_priorityRule = EventPriorityRule::HighestWeight;

        // Ignore any events from states that we are transitioning away from
        EE_REFLECT( Category = "Advanced Search Rules" );
        bool                                            m_ignoreInactiveBranchEvents = false;
    };

    //-------------------------------------------------------------------------

    class IDEventPercentageThroughToolsNode final : public FlowToolsNode
    {
        EE_REFLECT_TYPE( IDEventPercentageThroughToolsNode );

    public:

        IDEventPercentageThroughToolsNode();

        virtual char const* GetTypeName() const override { return "ID Event Percentage Through"; }
        virtual char const* GetCategory() const override { return "Events"; }
        virtual TBitFlags<GraphType> GetAllowedParentGraphTypes() const override { return TBitFlags<GraphType>( GraphType::TransitionConduit, GraphType::ValueTree ); }
        virtual int16_t Compile( GraphCompilationContext& context ) const override;
        virtual void DrawInfoText( NodeGraph::DrawContext const& ctx, NodeGraph::UserContext* pUserContext ) override;
        virtual void GetLogicAndEventIDs( TVector<StringID>& outIDs ) const override { outIDs.emplace_back( m_eventID ); }
        virtual void RenameLogicAndEventIDs( StringID oldID, StringID newID ) override { if ( m_eventID == oldID ) { NodeGraph::ScopedNodeModification snm( this ); m_eventID = newID; } }

    private:

        EE_REFLECT( Category = "Advanced Search Rules" );
        EventPriorityRule                               m_priorityRule = EventPriorityRule::HighestWeight;

        // When used in a transition, should we limit the search to only the source state? This is useful to ignore events from lower layers!
        EE_REFLECT( Category = "Advanced Search Rules" );
        bool                                            m_limitSearchToSourceState = false;

        // Ignore any events from states that we are transitioning away from
        EE_REFLECT( Category = "Advanced Search Rules" );
        bool                                            m_ignoreInactiveBranchEvents = false;

        EE_REFLECT( CustomEditor = "AnimGraph_ID" );
        StringID                                        m_eventID;
    };

    //-------------------------------------------------------------------------

    class GraphEventConditionToolsNode final : public FlowToolsNode
    {
        EE_REFLECT_TYPE( GraphEventConditionToolsNode );

        struct Condition : public IReflectedType
        {
            EE_REFLECT_TYPE( Condition );

            EE_REFLECT( CustomEditor = "AnimGraph_ID" );
            StringID                                    m_eventID;

            EE_REFLECT();
            GraphEventTypeCondition                     m_type = GraphEventTypeCondition::Any;
        };

    public:

        GraphEventConditionToolsNode();

        virtual char const* GetTypeName() const override { return "Graph Event Condition"; }
        virtual char const* GetCategory() const override { return "Events"; }
        virtual TBitFlags<GraphType> GetAllowedParentGraphTypes() const override { return TBitFlags<GraphType>( GraphType::TransitionConduit, GraphType::BlendTree, GraphType::ValueTree ); }
        virtual int16_t Compile( GraphCompilationContext& context ) const override;
        virtual void DrawInfoText( NodeGraph::DrawContext const& ctx, NodeGraph::UserContext* pUserContext ) override;
        virtual void GetLogicAndEventIDs( TVector<StringID>& outIDs ) const override;
        virtual void RenameLogicAndEventIDs( StringID oldID, StringID newID ) override;

    private:

        EE_REFLECT();
        EventConditionOperator                          m_operator = EventConditionOperator::Or;

        // When used in a transition, should we limit the search to only the source state? This is useful to ignore events from lower layers!
        EE_REFLECT( Category = "Advanced Search Rules" );
        bool                                            m_limitSearchToSourceState = false;

        // Ignore any events from states that we are transitioning away from
        EE_REFLECT( Category = "Advanced Search Rules" );
        bool                                            m_ignoreInactiveBranchEvents = false;

        EE_REFLECT( Category = "Conditions" );
        TVector<Condition>                              m_conditions;
    };

    //-------------------------------------------------------------------------

    class FootEventConditionToolsNode final : public FlowToolsNode
    {
        EE_REFLECT_TYPE( FootEventConditionToolsNode );

    public:

        FootEventConditionToolsNode();

        virtual char const* GetTypeName() const override { return "Footstep Condition"; }
        virtual char const* GetCategory() const override { return "Events"; }
        virtual TBitFlags<GraphType> GetAllowedParentGraphTypes() const override { return TBitFlags<GraphType>( GraphType::TransitionConduit, GraphType::ValueTree ); }
        virtual int16_t Compile( GraphCompilationContext& context ) const override;
        virtual void DrawInfoText( NodeGraph::DrawContext const& ctx, NodeGraph::UserContext* pUserContext ) override;

    private:

        EE_REFLECT();
        FootEvent::PhaseCondition                       m_phaseCondition = FootEvent::PhaseCondition::LeftFootDown;

        // When used in a transition, should we limit the search to only the source state? This is useful to ignore events from lower layers!
        EE_REFLECT( Category = "Advanced Search Rules" );
        bool                                            m_limitSearchToSourceState = false;

        // Ignore any events from states that we are transitioning away from
        EE_REFLECT( Category = "Advanced Search Rules" );
        bool                                            m_ignoreInactiveBranchEvents = false;
    };

    //-------------------------------------------------------------------------

    class FootstepEventPercentageThroughToolsNode final : public FlowToolsNode
    {
        EE_REFLECT_TYPE( FootstepEventPercentageThroughToolsNode );

    public:

        FootstepEventPercentageThroughToolsNode();

        virtual char const* GetTypeName() const override { return "Footstep Percentage Through"; }
        virtual char const* GetCategory() const override { return "Events"; }
        virtual TBitFlags<GraphType> GetAllowedParentGraphTypes() const override { return TBitFlags<GraphType>( GraphType::TransitionConduit ); }
        virtual int16_t Compile( GraphCompilationContext& context ) const override;
        virtual void DrawInfoText( NodeGraph::DrawContext const& ctx, NodeGraph::UserContext* pUserContext ) override;

    private:

        EE_REFLECT();
        FootEvent::PhaseCondition                       m_phaseCondition = FootEvent::PhaseCondition::LeftFootDown;

        EE_REFLECT( Category = "Advanced Search Rules" );
        EventPriorityRule                               m_priorityRule = EventPriorityRule::HighestWeight;

        // When used in a transition, should we limit the search to only the source state? This is useful to ignore events from lower layers!
        EE_REFLECT( Category = "Advanced Search Rules" );
        bool                                            m_limitSearchToSourceState = false;

        // Ignore any events from states that we are transitioning away from
        EE_REFLECT( Category = "Advanced Search Rules" );
        bool                                            m_ignoreInactiveBranchEvents = false;
    };

    //-------------------------------------------------------------------------

    class FootstepEventIDToolsNode final : public FlowToolsNode
    {
        EE_REFLECT_TYPE( FootstepEventIDToolsNode );

    public:

        FootstepEventIDToolsNode();

        virtual char const* GetTypeName() const override { return "Footstep Event ID"; }
        virtual char const* GetCategory() const override { return "Events"; }
        virtual TBitFlags<GraphType> GetAllowedParentGraphTypes() const override { return TBitFlags<GraphType>( GraphType::TransitionConduit ); }
        virtual int16_t Compile( GraphCompilationContext& context ) const override;

    private:

        EE_REFLECT( Category = "Advanced Search Rules" );
        EventPriorityRule                               m_priorityRule = EventPriorityRule::HighestWeight;

        // When used in a transition, should we limit the search to only the source state? This is useful to ignore events from lower layers!
        EE_REFLECT( Category = "Advanced Search Rules" );
        bool                                            m_limitSearchToSourceState = false;

        // Ignore any events from states that we are transitioning away from
        EE_REFLECT( Category = "Advanced Search Rules" );
        bool                                            m_ignoreInactiveBranchEvents = false;
    };

    //-------------------------------------------------------------------------

    class SyncEventIndexConditionToolsNode final : public FlowToolsNode
    {
        EE_REFLECT_TYPE( SyncEventIndexConditionToolsNode );

    public:

        SyncEventIndexConditionToolsNode();

        virtual char const* GetTypeName() const override { return "Sync Event Index Condition"; }
        virtual char const* GetCategory() const override { return "Events"; }
        virtual TBitFlags<GraphType> GetAllowedParentGraphTypes() const override { return TBitFlags<GraphType>( GraphType::TransitionConduit ); }
        virtual int16_t Compile( GraphCompilationContext& context ) const override;
        virtual void DrawInfoText( NodeGraph::DrawContext const& ctx, NodeGraph::UserContext* pUserContext ) override;

    private:

        EE_REFLECT();
        SyncEventIndexConditionNode::TriggerMode      m_triggerMode = SyncEventIndexConditionNode::TriggerMode::ExactlyAtEventIndex;

        EE_REFLECT();
        int32_t                                       m_syncEventIdx = InvalidIndex;
    };

    //-------------------------------------------------------------------------

    class CurrentSyncEventIDToolsNode final : public FlowToolsNode
    {
        EE_REFLECT_TYPE( CurrentSyncEventIDToolsNode );

    public:

        CurrentSyncEventIDToolsNode();

        virtual char const* GetTypeName() const override { return "Current Sync Event ID"; }
        virtual char const* GetCategory() const override { return "Events"; }
        virtual TBitFlags<GraphType> GetAllowedParentGraphTypes() const override { return TBitFlags<GraphType>( GraphType::TransitionConduit ); }
        virtual int16_t Compile( GraphCompilationContext& context ) const override;
    };

    //-------------------------------------------------------------------------

    class CurrentSyncEventIndexToolsNode final : public FlowToolsNode
    {
        EE_REFLECT_TYPE( CurrentSyncEventIndexToolsNode );

    public:

        CurrentSyncEventIndexToolsNode();

        virtual char const* GetTypeName() const override { return "Current Sync Event Index"; }
        virtual char const* GetCategory() const override { return "Events"; }
        virtual TBitFlags<GraphType> GetAllowedParentGraphTypes() const override { return TBitFlags<GraphType>( GraphType::TransitionConduit, GraphType::ValueTree ); }
        virtual int16_t Compile( GraphCompilationContext& context ) const override;
    };

    //-------------------------------------------------------------------------

    class CurrentSyncEventPercentageThroughToolsNode final : public FlowToolsNode
    {
        EE_REFLECT_TYPE( CurrentSyncEventPercentageThroughToolsNode );

    public:

        CurrentSyncEventPercentageThroughToolsNode();

        virtual char const* GetTypeName() const override { return "Current Sync Event Percentage Through"; }
        virtual char const* GetCategory() const override { return "Events"; }
        virtual TBitFlags<GraphType> GetAllowedParentGraphTypes() const override { return TBitFlags<GraphType>( GraphType::TransitionConduit, GraphType::ValueTree ); }
        virtual int16_t Compile( GraphCompilationContext& context ) const override;
    };

    //-------------------------------------------------------------------------

    class TransitionEventConditionToolsNode final : public FlowToolsNode
    {
        EE_REFLECT_TYPE( TransitionEventConditionToolsNode );

    public:

        TransitionEventConditionToolsNode();

        virtual char const* GetTypeName() const override { return "Transition Event Condition"; }
        virtual char const* GetCategory() const override { return "Events"; }
        virtual TBitFlags<GraphType> GetAllowedParentGraphTypes() const override { return TBitFlags<GraphType>( GraphType::TransitionConduit, GraphType::ValueTree ); }
        virtual int16_t Compile( GraphCompilationContext& context ) const override;
        virtual void DrawInfoText( NodeGraph::DrawContext const& ctx, NodeGraph::UserContext* pUserContext ) override;
        virtual void GetLogicAndEventIDs( TVector<StringID>& outIDs ) const override { outIDs.emplace_back( m_markerIDToMatch ); }
        virtual void RenameLogicAndEventIDs( StringID oldID, StringID newID ) override { if ( m_markerIDToMatch == oldID ) { NodeGraph::ScopedNodeModification snm( this ); m_markerIDToMatch = newID; } }

    private:

        EE_REFLECT();
        TransitionRuleCondition                         m_ruleCondition = TransitionRuleCondition::AnyAllowed;

        EE_REFLECT();
        bool                                            m_matchOnlySpecificMarkerID = false;

        EE_REFLECT( CustomEditor = "AnimGraph_ID" );
        StringID                                        m_markerIDToMatch;

        // When used in a transition, should we limit the search to only the source state? This is useful to ignore events from lower layers!
        EE_REFLECT( Category = "Advanced Search Rules" );
        bool                                            m_limitSearchToSourceState = false;

        // Ignore any events from states that we are transitioning away from
        EE_REFLECT( Category = "Advanced Search Rules" );
        bool                                            m_ignoreInactiveBranchEvents = false;
    };
}