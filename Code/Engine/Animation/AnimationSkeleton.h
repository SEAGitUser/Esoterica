#pragma once

#include "Engine/_Module/API.h"
#include "AnimationBoneMask.h"
#include "Base/Resource/IResource.h"
#include "Base/Math/Transform.h"
#include "Base/Types/BitFlags.h"

//-------------------------------------------------------------------------

namespace EE::Drawing { class DrawContext; }

//-------------------------------------------------------------------------

namespace EE::Animation
{
    // Per-bone flags to provide extra information
    enum class BoneFlags
    {
        None,
    };

    //-------------------------------------------------------------------------
    // Animation Skeleton
    //-------------------------------------------------------------------------

    class EE_ENGINE_API Skeleton : public Resource::IResource
    {
        EE_RESOURCE( 'skel', "Animation Skeleton", 7, false );
        EE_SERIALIZE( m_boneIDs, m_parentSpaceReferencePose, m_parentIndices, m_boneFlags, m_numBonesToSampleAtLowLOD );

        friend class SkeletonCompiler;
        friend class SkeletonLoader;

    public:

        enum class LOD : uint8_t
        {
            Low,
            High
        };

    public:

        #if EE_DEVELOPMENT_TOOLS
        static void DrawRootBone( Drawing::DrawContext& ctx, Transform const& transform );
        #endif

    public:

        virtual bool IsValid() const final;

        // Get the total number of bones in the skeleton
        inline int32_t GetNumBones() const { return (int32_t) m_boneIDs.size(); }

        // Get the number of bones to sample at a specific LOD
        inline int32_t GetNumBones( LOD lod ) const { return ( lod == LOD::Low ) ? m_numBonesToSampleAtLowLOD : GetNumBones(); }

        // Bone info
        //-------------------------------------------------------------------------

        EE_FORCE_INLINE bool IsValidBoneIndex( int32_t idx ) const { return idx >= 0 && idx < m_boneIDs.size(); }

        // Get the index for a given bone ID, can return InvalidIndex
        inline int32_t GetBoneIndex( StringID const& ID ) const { return VectorFindIndex( m_boneIDs, ID ); }

        // Get all parent indices
        inline TVector<int32_t> const& GetParentBoneIndices() const { return m_parentIndices; }

        // Get the direct parent for a given bone
        inline int32_t GetParentBoneIndex( int32_t idx ) const
        {
            EE_ASSERT( idx >= 0 && idx < m_parentIndices.size() );
            return m_parentIndices[idx];
        }

        // Find the index of the first child encountered for the specified bone. Returns InvalidIndex if this is a leaf bone.
        int32_t GetFirstChildBoneIndex( int32_t boneIdx ) const;

        // Returns whether the specified bone is a descendant of the specified parent bone (checks entire hierarchy, not just immediate parents)
        bool IsChildBoneOf( int32_t parentBoneIdx, int32_t childBoneIdx ) const;

        // Returns whether the specified bone is a parent of the specified child bone (checks entire hierarchy, not just immediate parents)
        EE_FORCE_INLINE bool IsParentBoneOf( int32_t parentBoneIdx, int32_t childBoneIdx ) const { return IsChildBoneOf( parentBoneIdx, childBoneIdx ); }

        // Returns whether the specified bone is a child of the specified parent bone
        EE_FORCE_INLINE bool AreBonesInTheSameHierarchy( int32_t boneIdx0, int32_t boneIdx1 ) const { return IsChildBoneOf( boneIdx0, boneIdx1) || IsChildBoneOf( boneIdx1, boneIdx0 ); }

        // Returns whether or not the specified bone has children
        EE_FORCE_INLINE bool IsLeafBone( int32_t boneIdx ) const { EE_ASSERT( IsValidBoneIndex( boneIdx ) ); return VectorContains( m_parentIndices, boneIdx ); }

        // Get the boneID for a specified bone index
        EE_FORCE_INLINE StringID GetBoneID( int32_t boneIdx ) const
        {
            EE_ASSERT( IsValidBoneIndex( boneIdx ) );
            return m_boneIDs[boneIdx];
        }

        // Get the LOD for a specific bone
        inline LOD GetBoneLOD( int32_t boneIdx ) const { return ( boneIdx > m_numBonesToSampleAtLowLOD ) ? LOD::High : LOD::Low; }

        // Will this bone be in a high LOD pose
        inline bool IsBoneHighLOD( int32_t boneIdx ) const { return boneIdx > m_numBonesToSampleAtLowLOD; }

        // Will this bone be in a low LOD pose
        inline bool IsBoneLowLOD( int32_t boneIdx ) const { return boneIdx <= m_numBonesToSampleAtLowLOD; }

        // Pose info
        //-------------------------------------------------------------------------

        TVector<Transform> const& GetParentSpaceReferencePose() const { return m_parentSpaceReferencePose; }
        TVector<Transform> const& GetModelSpaceReferencePose() const { return m_modelSpaceReferencePose; }

        // Get the parent space transform for a specified bone
        inline Transform const& GetBoneTransform( int32_t idx ) const
        {
            EE_ASSERT( idx >= 0 && idx < m_parentSpaceReferencePose.size() );
            return m_parentSpaceReferencePose[idx];
        }

        EE_FORCE_INLINE Transform const& GetBoneParentSpaceTransform( int32_t idx ) const { return GetBoneTransform( idx ); }

        // Get the parent space transform for a specified bone
        Transform GetBoneModelSpaceTransform( int32_t idx ) const;

        // Bone Masks
        //-------------------------------------------------------------------------

        uint32_t GetNumBoneMasks() const { return (uint32_t) m_boneMasks.size(); }
        int32_t GetBoneMaskIndex( StringID maskID ) const;
        BoneMask const* GetBoneMask( int32_t maskIdx ) const { return &m_boneMasks[maskIdx]; }
        BoneMask const* GetBoneMask( StringID maskID ) const;

        // Debug & Preview
        //-------------------------------------------------------------------------

        #if EE_DEVELOPMENT_TOOLS
        void DrawDebug( Drawing::DrawContext& ctx, Transform const& worldTransform ) const;
        inline ResourceID const& GetPreviewMeshID() const { return m_previewMeshID; }
        inline StringID GetPreviewAttachmentSocketID() const { return m_previewAttachmentSocketID; }
        #endif

    private:

        TVector<StringID>                   m_boneIDs;
        TVector<int32_t>                    m_parentIndices;
        TVector<Transform>                  m_parentSpaceReferencePose;
        TVector<Transform>                  m_modelSpaceReferencePose;
        TVector<TBitFlags<BoneFlags>>       m_boneFlags;
        TVector<BoneMask>                   m_boneMasks;
        int32_t                             m_numBonesToSampleAtLowLOD = 0; // The number of bones we should sample when operating at a low LOD

        #if EE_DEVELOPMENT_TOOLS
        ResourceID                          m_previewMeshID;
        StringID                            m_previewAttachmentSocketID;
        #endif
    };
}