#include "CodeGenerator.h"
#include "Base/TypeSystem/TypeID.h"
#include "Base/FileSystem/FileSystemUtils.h"
#include "Base/Utils/TopologicalSort.h"
#include <eastl/sort.h>
#include <fstream>
#include <string>

//-------------------------------------------------------------------------

namespace EE::TypeSystem::Reflection
{
    static bool SortTypesByDependencies( TVector<ReflectedType>& structureTypes )
    {
        int32_t const numTypes = (int32_t) structureTypes.size();
        if ( numTypes <= 1 )
        {
            return true;
        }

        // Create list to sort
        TVector<TopologicalSorter::Node > list;
        for ( auto i = 0; i < numTypes; i++ )
        {
            list.push_back( TopologicalSorter::Node( i ) );
        }

        for ( auto i = 0; i < numTypes; i++ )
        {
            for ( auto j = 0; j < numTypes; j++ )
            {
                if ( i != j && structureTypes[j].m_ID == structureTypes[i].m_parentID )
                {
                    list[i].m_children.push_back( &list[j] );
                }
            }
        }

        // Try to sort
        if ( !TopologicalSorter::Sort( list ) )
        {
            return false;
        }

        // Update type list
        TVector<ReflectedType> sortedTypes;
        sortedTypes.reserve( numTypes );

        for ( auto& node : list )
        {
            sortedTypes.push_back( structureTypes[node.m_ID] );
        }
        structureTypes.swap( sortedTypes );

        return true;
    }

    //-------------------------------------------------------------------------
    // Generator
    //-------------------------------------------------------------------------

    CodeGenerator::CodeGenerator( FileSystem::Path const& solutionDirectoryPath, ReflectionDatabase const& database )
        : m_solutionDirectoryPath( solutionDirectoryPath ), m_pDatabase( &database )
    {
        EE_ASSERT( solutionDirectoryPath.IsDirectoryPath() && solutionDirectoryPath.Exists() );
    }

    bool CodeGenerator::LogError( char const* pFormat, ... ) const
    {
        char buffer[256];

        va_list args;
        va_start( args, pFormat );
        VPrintf( buffer, 256, pFormat, args );
        va_end( args );

        m_errorMessage.assign( buffer );
        return false;
    }

    void CodeGenerator::LogWarning( char const* pFormat, ... ) const
    {
        char buffer[256];

        va_list args;
        va_start( args, pFormat );
        VPrintf( buffer, 256, pFormat, args );
        va_end( args );

        m_warningMessage.assign( buffer );
    }

    //-------------------------------------------------------------------------
    // Project Generation Functions
    //-------------------------------------------------------------------------

    bool CodeGenerator::GenerateCodeForSolution()
    {
        // Generate code per project
        //-------------------------------------------------------------------------

        for ( ReflectedProject const& prj : m_pDatabase->GetReflectedProjects() )
        {
            // Ignore module less projects
            if ( !prj.m_moduleHeaderID.IsValid() )
            {
                continue;
            }

            GenerateCodeForProject( prj );
        }

        // Generate solution type registration files
        //-------------------------------------------------------------------------

        if ( !GenerateSolutionTypeRegistrationFile( CompilationMode::Runtime ) )
        {
            return false;
        }

        if ( !GenerateSolutionTypeRegistrationFile( CompilationMode::Tools ) )
        {
            return false;
        }

        //-------------------------------------------------------------------------

        return true;
    }

    bool CodeGenerator::GenerateCodeForProject( ReflectedProject const& project )
    {
        // Ensure the auto generated directory exists
        //-------------------------------------------------------------------------

        FileSystem::Path const projectAutoGenDirectoryPath = project.m_typeInfoDirectoryPath;
        projectAutoGenDirectoryPath.EnsureDirectoryExists();

        // Generate code files for the dirty headers
        //-------------------------------------------------------------------------

        TVector<FileSystem::Path> generatedTypeInfoHeaders;

        for ( ReflectedHeader const& header : project.m_headerFiles )
        {
            if ( header.m_ID == project.m_moduleHeaderID )
            {
                continue;
            }

            TVector<ReflectedType> typesInHeader;
            m_pDatabase->GetAllTypesForHeader( header.m_ID, typesInHeader );
            if ( !typesInHeader.empty() )
            {
                // Generate typeinfo file for header
                generatedTypeInfoHeaders.emplace_back( header.m_typeInfoPath );
                if ( !GenerateTypeInfoFileForHeader( project, header, typesInHeader, generatedTypeInfoHeaders.back() ) )
                {
                    return false;
                }
            }
        }

        // Generate the module files
        //-------------------------------------------------------------------------

        if ( !GenerateProjectTypeInfoHeaderFile( project ) )
        {
            return false;
        }

        if ( !GenerateProjectTypeInfoSourceFile( project ) )
        {
            return false;
        }

        return true;
    }

    bool CodeGenerator::GenerateProjectTypeInfoHeaderFile( ReflectedProject const& project )
    {
        // Get all types in project
        TVector<ReflectedType> typesInProject;
        m_pDatabase->GetAllTypesForProject( project.m_ID, typesInProject );
        if ( !SortTypesByDependencies( typesInProject ) )
        {
            return LogError( "Cyclic header dependency detected in project: %s", project.m_name.c_str() );
        }

        // Header
        //-------------------------------------------------------------------------

        std::stringstream stream;
        stream.str( std::string() );
        stream.clear();
        stream << "//-------------------------------------------------------------------------\n";
        stream << "// This is an auto-generated file - DO NOT edit\n";
        stream << "//-------------------------------------------------------------------------\n";
        stream << "// Generated For: " << project.GetModuleHeader()->m_path.c_str() << "\n\n";
        stream << "#include \"../../API.h\"\n";
        stream << "#include \"Base/Esoterica.h\"\n\n";
        stream << "//-------------------------------------------------------------------------\n\n";

        stream << "namespace EE\n";
        stream << "{\n";
        stream << "    namespace TypeSystem { class TypeRegistry; }\n\n";
        stream << "    //-------------------------------------------------------------------------\n\n";

        // Type Registration functions
        //-------------------------------------------------------------------------

        for ( ReflectedType const& type : typesInProject )
        {
            InlineString typenameStr( InlineString::CtorSprintf(), "%s%s", type.m_namespace.c_str(), type.m_name.c_str() );
            StringUtils::ReplaceAllOccurrencesInPlace( typenameStr, "::", "_" );

            InlineString str;

            if ( type.m_isDevOnly )
            {
                str.sprintf( "    EE_DEVELOPMENT_TOOLS_ONLY( void RegisterType_%s( TypeSystem::TypeRegistry& typeRegistry ) );\n", typenameStr.c_str() );
                stream << str.c_str();

                if ( !type.IsAbstract() && !type.IsEnum() )
                {
                    str.sprintf( "    EE_DEVELOPMENT_TOOLS_ONLY( void CreateDefaultInstance_%s() );\n", typenameStr.c_str() );
                    stream << str.c_str();
                }

                str.sprintf( "    EE_DEVELOPMENT_TOOLS_ONLY( void UnregisterType_%s( TypeSystem::TypeRegistry& typeRegistry ) );\n\n", typenameStr.c_str() );
                stream << str.c_str();
            }
            else
            {
                str.sprintf( "    void RegisterType_%s( TypeSystem::TypeRegistry& typeRegistry );\n", typenameStr.c_str() );
                stream << str.c_str();

                if ( !type.IsAbstract() && !type.IsEnum() )
                {
                    str.sprintf( "    void CreateDefaultInstance_%s();\n", typenameStr.c_str() );
                    stream << str.c_str();
                }

                str.sprintf( "    void UnregisterType_%s( TypeSystem::TypeRegistry& typeRegistry );\n\n", typenameStr.c_str() );
                stream << str.c_str();
            }
        }

        // Module Registration Functions
        //-------------------------------------------------------------------------

        InlineString const moduleStr = StringUtils::ReplaceAllOccurrences<InlineString>( project.m_moduleClassName.c_str(), "::", "_" );

        stream << "    //-------------------------------------------------------------------------\n\n";
        stream << "    " << project.m_exportMacro.c_str() << " void " << moduleStr.c_str() << "_RegisterTypes( TypeSystem::TypeRegistry& typeRegistry );\n";
        stream << "    " << project.m_exportMacro.c_str() << " void " << moduleStr.c_str() << "_CreateDefaultInstances();\n";
        stream << "    " << project.m_exportMacro.c_str() << " void " << moduleStr.c_str() << "_UnregisterTypes( TypeSystem::TypeRegistry& typeRegistry );\n";
        stream << "}";

        // File
        //-------------------------------------------------------------------------

        GeneratedFile& file = m_generatedFiles.emplace_back();
        file.m_path = project.GetTypeInfoHeaderFilePath();
        file.m_contents = stream.str().c_str();

        //-------------------------------------------------------------------------

        return true;
    }

    bool CodeGenerator::GenerateProjectTypeInfoSourceFile( ReflectedProject const& project )
    {
        // Get all types in project
        TVector<ReflectedType> typesInProject;
        m_pDatabase->GetAllTypesForProject( project.m_ID, typesInProject );
        if ( !SortTypesByDependencies( typesInProject ) )
        {
            return LogError( "Cyclic header dependency detected in project: %s", project.m_name.c_str() );
        }

        // Header
        //-------------------------------------------------------------------------

        std::stringstream stream;
        stream.str( std::string() );
        stream.clear();
        stream << "//-------------------------------------------------------------------------\n";
        stream << "// This is an auto-generated file - DO NOT edit\n";
        stream << "//-------------------------------------------------------------------------\n\n";
        stream << "#include \"" << project.GetTypeInfoHeaderFilePath().c_str() << "\"\n\n";
        stream << "//-------------------------------------------------------------------------\n\n";

        // Module Registration Functions
        //-------------------------------------------------------------------------

        InlineString const moduleStr = StringUtils::ReplaceAllOccurrences<InlineString>( project.m_moduleClassName.c_str(), "::", "_" );

        stream << "namespace EE\n";
        stream << "{\n";
        stream << "    void " << moduleStr.c_str() << "_RegisterTypes( TypeSystem::TypeRegistry& typeRegistry )\n";
        stream << "    {\n";

        for ( ReflectedType const& type : typesInProject )
        {
            InlineString typenameStr( InlineString::CtorSprintf(), "%s%s", type.m_namespace.c_str(), type.m_name.c_str() );
            StringUtils::ReplaceAllOccurrencesInPlace( typenameStr, "::", "_" );

            InlineString str;

            if ( type.m_isDevOnly )
            {
                str.sprintf( "        EE_DEVELOPMENT_TOOLS_ONLY( RegisterType_%s( typeRegistry ) );\n", typenameStr.c_str() );
                stream << str.c_str();
            }
            else
            {
                str.sprintf( "        RegisterType_%s( typeRegistry );\n", typenameStr.c_str() );
                stream << str.c_str();
            }
        }

        stream << "    }\n\n";

        //-------------------------------------------------------------------------

        stream << "    void " << moduleStr.c_str() << "_CreateDefaultInstances()\n";
        stream << "    {\n";

        for ( ReflectedType const& type : typesInProject )
        {
            if ( type.IsAbstract() || type.IsEnum() )
            {
                continue;
            }

            InlineString typenameStr( InlineString::CtorSprintf(), "%s%s", type.m_namespace.c_str(), type.m_name.c_str() );
            StringUtils::ReplaceAllOccurrencesInPlace( typenameStr, "::", "_" );

            InlineString str;

            if ( type.m_isDevOnly )
            {
                str.sprintf( "        EE_DEVELOPMENT_TOOLS_ONLY( CreateDefaultInstance_%s() );\n", typenameStr.c_str() );
                stream << str.c_str();
            }
            else
            {
                str.sprintf( "        CreateDefaultInstance_%s();\n", typenameStr.c_str() );
                stream << str.c_str();
            }
        }

        stream << "    }\n\n";


        //-------------------------------------------------------------------------

        stream << "    void " << moduleStr.c_str() << "_UnregisterTypes( TypeSystem::TypeRegistry& typeRegistry )\n";
        stream << "    {\n";

        for ( auto iter = typesInProject.rbegin(); iter != typesInProject.rend(); ++iter )
        {
            InlineString typenameStr( InlineString::CtorSprintf(), "%s%s", iter->m_namespace.c_str(), iter->m_name.c_str() );
            StringUtils::ReplaceAllOccurrencesInPlace( typenameStr, "::", "_" );

            InlineString str;

            if ( iter->m_isDevOnly )
            {
                str.sprintf( "        EE_DEVELOPMENT_TOOLS_ONLY( UnregisterType_%s( typeRegistry ) );\n", typenameStr.c_str() );
                stream << str.c_str();
            }
            else
            {
                str.sprintf( "        UnregisterType_%s( typeRegistry );\n", typenameStr.c_str() );
                stream << str.c_str();
            }
        }

        stream << "    }\n";
        stream << "}";

        // File
        //-------------------------------------------------------------------------

        GeneratedFile& file = m_generatedFiles.emplace_back();
        file.m_path = project.GetTypeInfoSourceFilePath();
        file.m_contents = stream.str().c_str();

        //-------------------------------------------------------------------------

        return true;
    }

    bool CodeGenerator::GenerateSolutionTypeRegistrationFile( CompilationMode mode )
    {
        //-------------------------------------------------------------------------
        // PREPARE DATA
        //-------------------------------------------------------------------------

        // Get all project and sort them according to dependency order
        //-------------------------------------------------------------------------

        auto sortPredicate = [] ( ReflectedProject const& pA, ReflectedProject const& pB )
        {
            return pA.m_dependencyCount < pB.m_dependencyCount;
        };

        TVector<ReflectedProject> projects = m_pDatabase->GetReflectedProjects();

        for ( auto i = 0; i < projects.size(); i++ )
        {
            // Ignore tools modules for engine headers
            if ( mode == CompilationMode::Runtime )
            {
                if ( projects[i].m_isToolsProject )
                {
                    projects.erase_unsorted( projects.begin() + i );
                    i--;
                    continue;
                }
            }

            // Ignore module-less modules
            if ( !projects[i].m_moduleHeaderID.IsValid() )
            {
                projects.erase_unsorted( projects.begin() + i );
                i--;
                continue;
            }
        }

        eastl::sort( projects.begin(), projects.end(), sortPredicate );

        //-------------------------------------------------------------------------
        // GENERATE
        //-------------------------------------------------------------------------

        // Header
        //-------------------------------------------------------------------------

        std::stringstream stream;
        stream.str( std::string() );
        stream.clear();
        stream << "//-------------------------------------------------------------------------\n";
        stream << "// This is an auto-generated file - DO NOT edit\n";
        stream << "//-------------------------------------------------------------------------\n\n";
        stream << "#include \"Base/TypeSystem/TypeRegistry.h\"\n";
        stream << "#include \"Base/TypeSystem/ResourceInfo.h\"\n";
        stream << "#include \"Base/TypeSystem/DataFileInfo.h\"\n";

        // Module Includes
        //-------------------------------------------------------------------------

        stream << "\n//-------------------------------------------------------------------------\n\n";

        for ( auto i = 0; i < projects.size(); i++ )
        {
            stream << "#include \"" << projects[i].GetTypeInfoHeaderFilePath().c_str() << "\"\n";
        }

        stream << "\n//-------------------------------------------------------------------------\n\n";

        // Namespace
        //-------------------------------------------------------------------------

        stream << "namespace EE::TypeSystem::Reflection\n";
        stream << "{\n";

        // Resource Registration
        //-------------------------------------------------------------------------

        GenerateResourceRegistrationMethods( stream, mode );

        if ( mode == CompilationMode::Tools )
        {
            stream << "\n";
            GenerateDataFileRegistrationMethods( stream );
        }

        stream << "\n    //-------------------------------------------------------------------------\n\n";

        // Registration function
        //-------------------------------------------------------------------------

        stream << "    inline void RegisterTypes( TypeSystem::TypeRegistry& typeRegistry )\n";
        stream << "    {\n";

        stream << "        typeRegistry.RegisterInternalTypes();\n";
        stream << "\n        //-------------------------------------------------------------------------\n\n";

        for ( ReflectedProject const& ReflectedProject : projects )
        {
            InlineString const moduleStr = StringUtils::ReplaceAllOccurrences<InlineString>( ReflectedProject.m_moduleClassName.c_str(), "::", "_" );
            stream << "        " << moduleStr.c_str() << "_RegisterTypes( typeRegistry );\n";
        }

        stream << "\n        //-------------------------------------------------------------------------\n\n";

        for ( ReflectedProject const& ReflectedProject : projects )
        {
            InlineString const moduleStr = StringUtils::ReplaceAllOccurrences<InlineString>( ReflectedProject.m_moduleClassName.c_str(), "::", "_" );
            stream << "        " << moduleStr.c_str() << "_CreateDefaultInstances();\n";
        }

        stream << "\n        //-------------------------------------------------------------------------\n\n";
        stream << "        RegisterResourceTypes( typeRegistry );\n";

        if ( mode == CompilationMode::Tools )
        {
            stream << "        RegisterDataFileTypes( typeRegistry );\n";
        }

        stream << "    }\n\n";

        // Unregistration functions
        //-------------------------------------------------------------------------

        stream << "    inline void UnregisterTypes( TypeSystem::TypeRegistry& typeRegistry )\n";
        stream << "    {\n";

        for ( auto iter = projects.rbegin(); iter != projects.rend(); ++iter )
        {
            InlineString const moduleStr = StringUtils::ReplaceAllOccurrences<InlineString>( iter->m_moduleClassName.c_str(), "::", "_" );
            stream << "        " << moduleStr.c_str() << "_UnregisterTypes( typeRegistry );\n";
        }

        stream << "\n        //-------------------------------------------------------------------------\n\n";
        stream << "        UnregisterResourceTypes( typeRegistry );\n";

        if ( mode == CompilationMode::Tools )
        {
            stream << "        UnregisterDataFileTypes( typeRegistry );\n";
        }

        stream << "\n        //-------------------------------------------------------------------------\n\n";
        stream << "        typeRegistry.UnregisterInternalTypes();\n";

        stream << "    }\n";

        // Namespace
        //-------------------------------------------------------------------------

        stream << "}\n";

        // File
        //-------------------------------------------------------------------------

        GeneratedFile& file = m_generatedFiles.emplace_back();
        file.m_contents = stream.str().c_str();

        if ( mode == CompilationMode::Runtime )
        {
            file.m_path = ReflectedSolution::GetRuntimeTypeRegistrationPath( m_solutionDirectoryPath );
        }
        else
        {
            file.m_path = ReflectedSolution::GetToolsTypeRegistrationPath( m_solutionDirectoryPath );
        }

        return true;
    }

    bool CodeGenerator::GenerateTypeInfoFileForHeader( ReflectedProject const& project, ReflectedHeader const& header, TVector<ReflectedType> const& typesInHeader, FileSystem::Path const& outputPath )
    {
        // File Header
        //-------------------------------------------------------------------------

        std::stringstream stream;
        stream.str( std::string() );
        stream.clear();
        stream << "#pragma once\n\n";
        stream << "//*************************************************************************\n";
        stream << "// This is an auto-generated file - DO NOT edit\n";
        stream << "//*************************************************************************\n\n";
        stream << "#include \"" << header.m_path.c_str() << "\"\n";
        stream << "#include \"Base/TypeSystem/TypeRegistry.h\"\n";
        stream << "#include \"Base/TypeSystem/EnumInfo.h\"\n";
        stream << "#include \"Base/Resource/ResourceTypeID.h\"\n";
        stream << "#include \"Base/Resource/ResourceSystem.h\"\n\n";

        // Get all types for the header
        //-------------------------------------------------------------------------

        InlineString str;
        for ( ReflectedType const& typeInfo : typesInHeader )
        {
            // Generate TypeInfo
            //-------------------------------------------------------------------------

            if ( typeInfo.IsEnum() )
            {
                GenerateEnumTypeInfo( stream, project.m_exportMacro, typeInfo );
            }
            else
            {
                // Validation
                if ( !typeInfo.m_parentID.IsValid() )
                {
                    String const fullTypeName = typeInfo.m_namespace + typeInfo.m_name;
                    return LogError( "Invalid parent hierarchy for type (%s), all registered types must derived from a registered type.", fullTypeName.c_str() );
                }

                ReflectedType const* pParentTypeInfo = m_pDatabase->GetType( typeInfo.m_parentID );
                EE_ASSERT( pParentTypeInfo != nullptr );
                GenerateStructureTypeInfo( stream, project.m_exportMacro, typeInfo, *pParentTypeInfo );
            }

            // Generate Component Methods
            //-------------------------------------------------------------------------

            if ( typeInfo.IsEntityComponent() )
            {
                GenerateComponentCodegen( stream, typeInfo );
            }

            // Generate Registration/Unregistration Methods
            //-------------------------------------------------------------------------

            InlineString typenameStr( InlineString::CtorSprintf(), "%s%s", typeInfo.m_namespace.c_str(), typeInfo.m_name.c_str() );
            StringUtils::ReplaceAllOccurrencesInPlace( typenameStr, "::", "_" );

            stream << "namespace EE\n";
            stream << "{\n";

            if ( typeInfo.m_isDevOnly )
            {
                str.sprintf( "    EE_DEVELOPMENT_TOOLS_ONLY( void RegisterType_%s( TypeSystem::TypeRegistry& typeRegistry ) { TypeSystem::TTypeInfo<%s%s>::RegisterType( typeRegistry ); } );\n", typenameStr.c_str(), typeInfo.m_namespace.c_str(), typeInfo.m_name.c_str() );
                stream << str.c_str();

                if ( !typeInfo.IsAbstract() && !typeInfo.IsEnum() )
                {
                    str.sprintf( "    EE_DEVELOPMENT_TOOLS_ONLY( void CreateDefaultInstance_%s() { TypeSystem::TTypeInfo<%s%s>::CreateDefaultInstance(); } );\n", typenameStr.c_str(), typeInfo.m_namespace.c_str(), typeInfo.m_name.c_str() );
                    stream << str.c_str();
                }

                str.sprintf( "    EE_DEVELOPMENT_TOOLS_ONLY( void UnregisterType_%s( TypeSystem::TypeRegistry& typeRegistry ) { TypeSystem::TTypeInfo<%s%s>::UnregisterType( typeRegistry ); } );\n", typenameStr.c_str(), typeInfo.m_namespace.c_str(), typeInfo.m_name.c_str() );
                stream << str.c_str();
            }
            else
            {
                str.sprintf( "    void RegisterType_%s( TypeSystem::TypeRegistry& typeRegistry ) { TypeSystem::TTypeInfo<%s%s>::RegisterType( typeRegistry ); }\n", typenameStr.c_str(), typeInfo.m_namespace.c_str(), typeInfo.m_name.c_str() );
                stream << str.c_str();

                if ( !typeInfo.IsAbstract() && !typeInfo.IsEnum() )
                {
                    str.sprintf( "    void CreateDefaultInstance_%s() { TypeSystem::TTypeInfo<%s%s>::CreateDefaultInstance(); };\n", typenameStr.c_str(), typeInfo.m_namespace.c_str(), typeInfo.m_name.c_str() );
                    stream << str.c_str();
                }

                str.sprintf( "    void UnregisterType_%s( TypeSystem::TypeRegistry& typeRegistry ) { TypeSystem::TTypeInfo<%s%s>::UnregisterType( typeRegistry ); }\n", typenameStr.c_str(), typeInfo.m_namespace.c_str(), typeInfo.m_name.c_str() );
                stream << str.c_str();
            }

            stream << "}\n\n";
        }

        // File
        //-------------------------------------------------------------------------

        GeneratedFile& file = m_generatedFiles.emplace_back();
        file.m_path = outputPath;
        file.m_contents = stream.str().c_str();

        return true;
    }

    //-------------------------------------------------------------------------
    // Resources
    //-------------------------------------------------------------------------

    bool CodeGenerator::GenerateResourceRegistrationMethods( std::stringstream& outputFileStream, CompilationMode mode )
    {
        TVector<ReflectedResourceType> const& registeredResourceTypes = m_pDatabase->GetAllRegisteredResourceTypes();

        // Registration function
        //-------------------------------------------------------------------------

        outputFileStream << "    inline void RegisterResourceTypes( TypeSystem::TypeRegistry& typeRegistry )\n";
        outputFileStream << "    {\n";

        if ( !registeredResourceTypes.empty() )
        {
            outputFileStream << "        TypeSystem::ResourceInfo resourceInfo;\n";
        }

        auto GetResourceTypeIDForTypeID = [&registeredResourceTypes] ( TypeID typeID )
        {
            for ( auto const& registeredResourceType : registeredResourceTypes )
            {
                if ( registeredResourceType.m_typeID == typeID )
                {
                    return registeredResourceType.m_resourceTypeID;
                }
            }

            EE_UNREACHABLE_CODE();
            return ResourceTypeID();
        };

        for ( auto& registeredResourceType : registeredResourceTypes )
        {
            if ( mode == CompilationMode::Runtime && registeredResourceType.m_isDevOnly )
            {
                continue;
            }

            TInlineString<5> const resourceTypeIDStr = registeredResourceType.m_resourceTypeID.ToString();

            outputFileStream << "\n";
            outputFileStream << "        resourceInfo.m_typeID = TypeSystem::TypeID( \"" << registeredResourceType.m_typeID.c_str() << "\");\n";
            outputFileStream << "        resourceInfo.m_resourceTypeID = ResourceTypeID( \"" << registeredResourceType.m_resourceTypeID.ToString().c_str() << "\" );\n";
            outputFileStream << "        resourceInfo.m_parentTypes.clear();\n";

            for ( auto const& parentType : registeredResourceType.m_parents )
            {
                ResourceTypeID const resourceTypeID = GetResourceTypeIDForTypeID( parentType );
                outputFileStream << "        resourceInfo.m_parentTypes.emplace_back( ResourceTypeID( \"" << resourceTypeID.ToString().c_str() << "\" ) );\n";
            }

            outputFileStream << "        EE_DEVELOPMENT_TOOLS_ONLY( resourceInfo.m_friendlyName = \"" << registeredResourceType.m_friendlyName.c_str() << "\" );\n";
            outputFileStream << "        typeRegistry.RegisterResourceTypeID( resourceInfo );\n";
        }

        outputFileStream << "    }\n\n";

        // Unregistration functions
        //-------------------------------------------------------------------------

        outputFileStream << "    inline void UnregisterResourceTypes( TypeSystem::TypeRegistry& typeRegistry )\n";
        outputFileStream << "    {\n";

        for ( auto iter = registeredResourceTypes.rbegin(); iter != registeredResourceTypes.rend(); ++iter )
        {
            if ( mode == CompilationMode::Runtime && iter->m_isDevOnly )
            {
                continue;
            }

            TInlineString<5> const resourceTypeIDStr = iter->m_resourceTypeID.ToString();

            outputFileStream << "        typeRegistry.UnregisterResourceTypeID( ResourceTypeID( \"" << resourceTypeIDStr.c_str() << "\" ) );\n";
        }

        outputFileStream << "    }\n";

        return true;
    }

    bool CodeGenerator::GenerateDataFileRegistrationMethods( std::stringstream& outputFileStream )
    {
        TVector<ReflectedDataFileType> const& registeredResourceTypes = m_pDatabase->GetAllRegisteredDataFileTypes();

        // Registration function
        //-------------------------------------------------------------------------

        outputFileStream << "    inline void RegisterDataFileTypes( TypeSystem::TypeRegistry& typeRegistry )\n";
        outputFileStream << "    {\n";

        if ( !registeredResourceTypes.empty() )
        {
            outputFileStream << "        TypeSystem::DataFileInfo dataFileInfo;\n";
        }

        for ( auto& registeredResourceType : registeredResourceTypes )
        {
            outputFileStream << "\n";
            outputFileStream << "        dataFileInfo.m_typeID = TypeSystem::TypeID( \"" << registeredResourceType.m_typeID.c_str() << "\");\n";
            outputFileStream << "        dataFileInfo.m_extensionFourCC = " << registeredResourceType.m_extensionFourCC << ";\n";
            outputFileStream << "        dataFileInfo.m_friendlyName = \"" << registeredResourceType.m_friendlyName.c_str() << "\";\n";
            outputFileStream << "        typeRegistry.RegisterDataFileInfo( dataFileInfo );\n";
        }

        outputFileStream << "    }\n\n";

        // Unregistration functions
        //-------------------------------------------------------------------------

        outputFileStream << "    inline void UnregisterDataFileTypes( TypeSystem::TypeRegistry& typeRegistry )\n";
        outputFileStream << "    {\n";

        for ( auto iter = registeredResourceTypes.rbegin(); iter != registeredResourceTypes.rend(); ++iter )
        {
            outputFileStream << "        typeRegistry.UnregisterDataFileInfo( TypeSystem::TypeID( \"" << iter->m_typeID.c_str() << "\" ) );\n";
        }

        outputFileStream << "    }\n";

        return true;
    }

    //-------------------------------------------------------------------------
    // Type Info
    //-------------------------------------------------------------------------

    void CodeGenerator::GenerateEnumTypeInfo( std::stringstream& outputFileStream, String const& exportMacro, ReflectedType const& typeInfo )
    {
        String const fullTypeName( typeInfo.m_namespace + typeInfo.m_name );

        TInlineVector<int32_t, 50> sortingConstantIndices; // Sorted list of constant indices
        TInlineVector<int32_t, 50> sortedOrder; // Final order for each constant

        for ( auto i = 0u; i < typeInfo.m_enumConstants.size(); i++ )
        {
            sortingConstantIndices.emplace_back( i );
        }

        auto Comparator = [&typeInfo] ( int32_t a, int32_t b )
        {
            auto const& elemA = typeInfo.m_enumConstants[a];
            auto const& elemB = typeInfo.m_enumConstants[b];
            return elemA.m_label < elemB.m_label;
        };

        eastl::sort( sortingConstantIndices.begin(), sortingConstantIndices.end(), Comparator );

        sortedOrder.resize( sortingConstantIndices.size() );

        for ( auto i = 0u; i < typeInfo.m_enumConstants.size(); i++ )
        {
            sortedOrder[sortingConstantIndices[i]] = i;
        }

        //-------------------------------------------------------------------------

        outputFileStream << "//-------------------------------------------------------------------------\n";
        outputFileStream << "// Enum Info: " << fullTypeName.c_str() << "\n";
        outputFileStream << "//-------------------------------------------------------------------------\n\n";

        if ( typeInfo.m_isDevOnly )
        {
            outputFileStream << "#if EE_DEVELOPMENT_TOOLS\n";
        }

        outputFileStream << "namespace EE::TypeSystem\n";
        outputFileStream << "{\n";
        outputFileStream << "    template<>\n";
        outputFileStream << "    class TTypeInfo<" << fullTypeName.c_str() << "> final : public TypeInfo\n";
        outputFileStream << "    {\n";
        outputFileStream << "        static TypeInfo* s_pInstance;\n\n";
        outputFileStream << "    public:\n\n";

        // Static registration Function
        //-------------------------------------------------------------------------

        outputFileStream << "        static void RegisterType( TypeSystem::TypeRegistry& typeRegistry )\n";
        outputFileStream << "        {\n";
        outputFileStream << "            EE_ASSERT( s_pInstance == nullptr );\n";
        outputFileStream << "            s_pInstance = EE::New<" << " TTypeInfo<" << fullTypeName.c_str() << ">>();\n";
        outputFileStream << "            s_pInstance->m_ID = TypeSystem::TypeID( \"" << fullTypeName.c_str() << "\" );\n";
        outputFileStream << "            s_pInstance->m_size = sizeof( " << fullTypeName.c_str() << " );\n";
        outputFileStream << "            s_pInstance->m_alignment = alignof( " << fullTypeName.c_str() << " );\n";
        outputFileStream << "            typeRegistry.RegisterType( s_pInstance );\n\n";

        outputFileStream << "            TypeSystem::EnumInfo enumInfo;\n";
        outputFileStream << "            enumInfo.m_ID = TypeSystem::TypeID( \"" << fullTypeName.c_str() << "\" );\n";

        switch ( typeInfo.m_underlyingType )
        {
            case TypeSystem::CoreTypeID::Uint8:
            outputFileStream << "            enumInfo.m_underlyingType = TypeSystem::CoreTypeID::Uint8;\n";
            break;

            case TypeSystem::CoreTypeID::Int8:
            outputFileStream << "            enumInfo.m_underlyingType = TypeSystem::CoreTypeID::Int8;\n";
            break;

            case TypeSystem::CoreTypeID::Uint16:
            outputFileStream << "            enumInfo.m_underlyingType = TypeSystem::CoreTypeID::Uint16;\n";
            break;

            case TypeSystem::CoreTypeID::Int16:
            outputFileStream << "            enumInfo.m_underlyingType = TypeSystem::CoreTypeID::Int16;\n";
            break;

            case TypeSystem::CoreTypeID::Uint32:
            outputFileStream << "            enumInfo.m_underlyingType = TypeSystem::CoreTypeID::Uint32;\n";
            break;

            case TypeSystem::CoreTypeID::Int32:
            outputFileStream << "            enumInfo.m_underlyingType = TypeSystem::CoreTypeID::Int32;\n";
            break;

            default:
            EE_HALT();
            break;
        }

        outputFileStream << "\n";
        outputFileStream << "            //-------------------------------------------------------------------------\n\n";

        outputFileStream << "            TypeSystem::EnumInfo::ConstantInfo constantInfo;\n";

        for ( auto i = 0u; i < typeInfo.m_enumConstants.size(); i++ )
        {
            String escapedDescription = typeInfo.m_enumConstants[i].m_description;
            StringUtils::ReplaceAllOccurrencesInPlace( escapedDescription, "\"", "\\\"" );

            outputFileStream << "\n";
            outputFileStream << "            constantInfo.m_ID = StringID( \"" << typeInfo.m_enumConstants[i].m_label.c_str() << "\" );\n";
            outputFileStream << "            constantInfo.m_value = " << typeInfo.m_enumConstants[i].m_value << ";\n";
            outputFileStream << "            constantInfo.m_alphabeticalOrder = " << sortedOrder[i] << ";\n";
            outputFileStream << "            EE_DEVELOPMENT_TOOLS_ONLY( constantInfo.m_description = \"" << escapedDescription.c_str() << "\" );\n";
            outputFileStream << "            enumInfo.m_constants.emplace_back( constantInfo );\n";
        }

        outputFileStream << "\n";
        outputFileStream << "            //-------------------------------------------------------------------------\n\n";
        outputFileStream << "            typeRegistry.RegisterEnum( enumInfo );\n";
        outputFileStream << "        }\n\n";

        // Static unregistration Function
        //-------------------------------------------------------------------------

        outputFileStream << "        static void UnregisterType( TypeSystem::TypeRegistry& typeRegistry )\n";
        outputFileStream << "        {\n";
        outputFileStream << "            EE_ASSERT( s_pInstance != nullptr );\n";
        outputFileStream << "            typeRegistry.UnregisterEnum( s_pInstance->m_ID );\n";
        outputFileStream << "            typeRegistry.UnregisterType( s_pInstance );\n";
        outputFileStream << "            EE::Delete( s_pInstance );\n";
        outputFileStream << "        }\n\n";

        // Constructor
        //-------------------------------------------------------------------------

        outputFileStream << "    public:\n\n";

        outputFileStream << "        TTypeInfo()\n";
        outputFileStream << "        {\n";

        // Create type info
        outputFileStream << "            m_ID = TypeSystem::TypeID( \"" << fullTypeName.c_str() << "\" );\n";
        outputFileStream << "            m_size = sizeof( " << fullTypeName.c_str() << " );\n";
        outputFileStream << "            m_alignment = alignof( " << fullTypeName.c_str() << " );\n\n";

        // Create dev tools info
        outputFileStream << "            EE_DEVELOPMENT_TOOLS_ONLY( m_friendlyName = \"" << typeInfo.GetFriendlyName().c_str() << "\" );\n";
        outputFileStream << "            EE_DEVELOPMENT_TOOLS_ONLY( m_namespace = \"" << typeInfo.GetInternalNamespace().c_str() << "\" );\n";
        outputFileStream << "            EE_DEVELOPMENT_TOOLS_ONLY( m_category = \"" << typeInfo.GetCategory().c_str() << "\" );\n";

        outputFileStream << "        }\n\n";

        // Implement required virtual methods
        //-------------------------------------------------------------------------

        outputFileStream << "        virtual void CopyProperties( IReflectedType* pTypeInstance, IReflectedType const* pRHS ) const override { EE_HALT(); }\n";
        outputFileStream << "        virtual IReflectedType* CreateType() const override { EE_HALT(); return nullptr; }\n";
        outputFileStream << "        virtual void CreateTypeInPlace( IReflectedType* pAllocatedMemory ) const override { EE_HALT(); }\n";
        outputFileStream << "        virtual void ResetType(IReflectedType* pTypeInstance ) const override { EE_HALT(); }\n";
        outputFileStream << "        virtual void LoadResources( Resource::ResourceSystem* pResourceSystem, Resource::ResourceRequesterID const& requesterID, IReflectedType * pType ) const override { EE_HALT(); }\n";
        outputFileStream << "        virtual void UnloadResources( Resource::ResourceSystem* pResourceSystem, Resource::ResourceRequesterID const& requesterID, IReflectedType * pType ) const override { EE_HALT(); }\n";
        outputFileStream << "        virtual LoadingStatus GetResourceLoadingStatus( IReflectedType* pType ) const override { EE_HALT(); return LoadingStatus::Failed; }\n";
        outputFileStream << "        virtual LoadingStatus GetResourceUnloadingStatus( IReflectedType* pType ) const override { EE_HALT(); return LoadingStatus::Failed; }\n";
        outputFileStream << "        virtual ResourceTypeID GetExpectedResourceTypeForProperty( IReflectedType * pType, uint64_t propertyID ) const override { EE_HALT(); return ResourceTypeID(); }\n";
        outputFileStream << "        virtual void GetReferencedResources( IReflectedType const* pType, TVector<ResourceID>&outReferencedResources ) const override {};\n";
        outputFileStream << "        virtual uint8_t* GetArrayElementDataPtr( IReflectedType* pTypeInstance, uint64_t arrayID, size_t arrayIdx ) const override { EE_HALT(); return 0; }\n";
        outputFileStream << "        virtual size_t GetArraySize( IReflectedType const* pTypeInstance, uint64_t arrayID ) const override { EE_HALT(); return 0; }\n";
        outputFileStream << "        virtual size_t GetArrayElementSize( uint64_t arrayID ) const override { EE_HALT(); return 0; }\n";
        outputFileStream << "        virtual void SetArraySize( IReflectedType* pTypeInstance, uint64_t arrayID, size_t size ) const override { EE_HALT(); }\n";
        outputFileStream << "        virtual void ClearArray( IReflectedType* pTypeInstance, uint64_t arrayID ) const override { EE_HALT(); }\n";
        outputFileStream << "        virtual void AddArrayElement( IReflectedType* pTypeInstance, uint64_t arrayID ) const override { EE_HALT(); }\n";
        outputFileStream << "        virtual void InsertArrayElement( IReflectedType* pTypeInstance, uint64_t arrayID, size_t insertIdx ) const override { EE_HALT(); }\n";
        outputFileStream << "        virtual void MoveArrayElement( IReflectedType* pTypeInstance, uint64_t arrayID, size_t originalElementIdx, size_t newElementIdx ) const override { EE_HALT(); }\n";
        outputFileStream << "        virtual void RemoveArrayElement( IReflectedType* pTypeInstance, uint64_t arrayID, size_t arrayIdx ) const override { EE_HALT(); }\n";
        outputFileStream << "        virtual bool AreAllPropertyValuesEqual( IReflectedType const* pTypeInstance, IReflectedType const* pOtherTypeInstance ) const override { EE_HALT(); return false; }\n";
        outputFileStream << "        virtual bool IsPropertyValueEqual( IReflectedType const* pTypeInstance, IReflectedType const* pOtherTypeInstance, uint64_t propertyID, int32_t arrayIdx = InvalidIndex ) const override { EE_HALT(); return false; }\n";
        outputFileStream << "        virtual void ResetToDefault( IReflectedType* pTypeInstance, uint64_t propertyID ) const override { EE_HALT(); }\n";

        //-------------------------------------------------------------------------

        outputFileStream << "    };\n\n";

        outputFileStream << "    TypeInfo* TTypeInfo<" << fullTypeName.c_str() << ">::s_pInstance = nullptr;\n";

        outputFileStream << "}\n";

        if ( typeInfo.m_isDevOnly )
        {
            outputFileStream << "#endif\n";
        }

        outputFileStream << "\n";
    }

    void CodeGenerator::GenerateStructureTypeInfo( std::stringstream& outputFileStream, String const& exportMacro, ReflectedType const& typeInfo, ReflectedType const& parentType )
    {
        String const fullTypeName( typeInfo.m_namespace + typeInfo.m_name );

        // Header
        //-------------------------------------------------------------------------

        outputFileStream << "//-------------------------------------------------------------------------\n";
        outputFileStream << "// TypeInfo: " << fullTypeName.c_str() << "\n";
        outputFileStream << "//-------------------------------------------------------------------------\n\n";

        // Dev Flag
        if ( typeInfo.m_isDevOnly )
        {
            outputFileStream << "#if EE_DEVELOPMENT_TOOLS\n";
        }

        // Type Info
        //-------------------------------------------------------------------------

        outputFileStream << "namespace EE\n";
        outputFileStream << "{\n";
        outputFileStream << "    namespace TypeSystem\n";
        outputFileStream << "    {\n";
        outputFileStream << "        template<>\n";
        outputFileStream << "        class TTypeInfo<" << fullTypeName.c_str() << "> final : public TypeInfo\n";
        outputFileStream << "        {\n";
        outputFileStream << "        public:\n\n";

        GenerateTypeInfoStaticTypeRegistrationMethods( outputFileStream, typeInfo );

        outputFileStream << "        public:\n\n";

        GenerateTypeInfoConstructor( outputFileStream, typeInfo, parentType );
        GenerateTypeInfoCreationMethod( outputFileStream, typeInfo );
        GenerateTypeInfoInPlaceCreationMethod( outputFileStream, typeInfo );
        GenerateTypeInfoResetTypeMethod( outputFileStream, typeInfo );
        GenerateTypeInfoLoadResourcesMethod( outputFileStream, typeInfo );
        GenerateTypeInfoUnloadResourcesMethod( outputFileStream, typeInfo );
        GenerateTypeInfoResourceLoadingStatusMethod( outputFileStream, typeInfo );
        GenerateTypeInfoResourceUnloadingStatusMethod( outputFileStream, typeInfo );
        GenerateTypeInfoGetReferencedResourceMethod( outputFileStream, typeInfo );
        GenerateTypeInfoExpectedResourceTypeMethod( outputFileStream, typeInfo );
        GenerateTypeInfoArrayAccessorMethod( outputFileStream, typeInfo );
        GenerateTypeInfoArraySizeMethod( outputFileStream, typeInfo );
        GenerateTypeInfoArrayElementSizeMethod( outputFileStream, typeInfo );
        GenerateTypeInfoArraySetSizeMethod( outputFileStream, typeInfo );
        GenerateTypeInfoArrayClearMethod( outputFileStream, typeInfo );
        GenerateTypeInfoAddArrayElementMethod( outputFileStream, typeInfo );
        GenerateTypeInfoInsertArrayElementMethod( outputFileStream, typeInfo );
        GenerateTypeInfoMoveArrayElementMethod( outputFileStream, typeInfo );
        GenerateTypeInfoRemoveArrayElementMethod( outputFileStream, typeInfo );
        GenerateTypeInfoCopyProperties( outputFileStream, typeInfo );
        GenerateTypeInfoAreAllPropertiesEqualMethod( outputFileStream, typeInfo );
        GenerateTypeInfoIsPropertyEqualMethod( outputFileStream, typeInfo );
        GenerateTypeInfoSetToDefaultValueMethod( outputFileStream, typeInfo );

        outputFileStream << "        };\n";
        outputFileStream << "    }\n";
        outputFileStream << "}\n";

        // Dev Flag
        //-------------------------------------------------------------------------

        if ( typeInfo.m_isDevOnly )
        {
            outputFileStream << "#endif\n";
        }

        outputFileStream << "\n";
    }

    void CodeGenerator::GenerateComponentCodegen( std::stringstream& outputFileStream, ReflectedType const& typeInfo )
    {
        String const fullTypeName( typeInfo.m_namespace + typeInfo.m_name );

        // Header
        //-------------------------------------------------------------------------

        outputFileStream << "//-------------------------------------------------------------------------\n";
        outputFileStream << "// Component: " << fullTypeName.c_str() << "\n";
        outputFileStream << "//-------------------------------------------------------------------------\n\n";

        // Dev Flag
        //-------------------------------------------------------------------------

        if ( typeInfo.m_isDevOnly )
        {
            outputFileStream << "#if EE_DEVELOPMENT_TOOLS\n";
        }

        // Generate entity component methods
        //-------------------------------------------------------------------------

        if ( typeInfo.IsEntityComponent() )
        {
            // Generate Load Method
            //-------------------------------------------------------------------------

            outputFileStream << "void " << fullTypeName.c_str() << "::Load( EntityModel::LoadingContext const& context, Resource::ResourceRequesterID const& requesterID )\n";
            outputFileStream << "{\n";

            if ( typeInfo.HasProperties() )
            {
                outputFileStream << "    " << fullTypeName.c_str() << "::s_pTypeInfo->LoadResources( context.m_pResourceSystem, requesterID, this );\n";
                outputFileStream << "    m_status = Status::Loading;\n";
            }
            else
            {
                outputFileStream << "    m_status = Status::Loaded;\n";
            }

            outputFileStream << "}\n";

            // Generate Unload Method
            //-------------------------------------------------------------------------

            outputFileStream << "\n";
            outputFileStream << "void " << fullTypeName.c_str() << "::Unload( EntityModel::LoadingContext const& context, Resource::ResourceRequesterID const& requesterID )\n";
            outputFileStream << "{\n";

            if ( typeInfo.HasProperties() )
            {
                outputFileStream << "    " << fullTypeName.c_str() << "::s_pTypeInfo->UnloadResources( context.m_pResourceSystem, requesterID, this );\n";
            }

            outputFileStream << "    m_status = Status::Unloaded;\n";
            outputFileStream << "}\n";

            // Generate Update Status Method
            //-------------------------------------------------------------------------

            outputFileStream << "\n";
            outputFileStream << "void " << fullTypeName.c_str() << "::UpdateLoading()\n";
            outputFileStream << "{\n";
            outputFileStream << "    if( m_status == Status::Loading )\n";
            outputFileStream << "    {\n";

            {
                if ( typeInfo.HasProperties() )
                {
                    // Wait for resources to be loaded
                    outputFileStream << "        auto const resourceLoadingStatus = " << fullTypeName.c_str() << "::s_pTypeInfo->GetResourceLoadingStatus( this );\n";
                    outputFileStream << "        if ( resourceLoadingStatus == LoadingStatus::Loading )\n";
                    outputFileStream << "        {\n";
                    outputFileStream << "           return; // Something is still loading so early-out\n";
                    outputFileStream << "        }\n\n";

                    // Set status
                    outputFileStream << "        if ( resourceLoadingStatus == LoadingStatus::Failed )\n";
                    outputFileStream << "        {\n";
                    outputFileStream << "           m_status = EntityComponent::Status::LoadingFailed;\n";
                    outputFileStream << "        }\n";
                    outputFileStream << "        else\n";
                    outputFileStream << "        {\n";
                    outputFileStream << "           m_status = EntityComponent::Status::Loaded;\n";
                    outputFileStream << "        }\n";
                }
                else
                {
                    outputFileStream << "        m_status = EntityComponent::Status::Loaded;\n";
                }
            }

            outputFileStream << "    }\n";
            outputFileStream << "}\n";
        }

        // Dev Flag
        //-------------------------------------------------------------------------

        if ( typeInfo.m_isDevOnly )
        {
            outputFileStream << "#endif\n";
        }

        outputFileStream << "\n";
    }

    //-------------------------------------------------------------------------
    // Structure Type Info
    //-------------------------------------------------------------------------

    void CodeGenerator::GenerateTypeInfoCreationMethod( std::stringstream& file, ReflectedType const& type )
    {
        file << "            virtual IReflectedType* CreateType() const override final\n";
        file << "            {\n";
        if ( !type.IsAbstract() )
        {
            file << "                auto pMemory = EE::Alloc( sizeof( " << type.m_namespace.c_str() << type.m_name.c_str() << " ), alignof( " << type.m_namespace.c_str() << type.m_name.c_str() << " ) );\n";
            file << "                return new ( pMemory ) " << type.m_namespace.c_str() << type.m_name.c_str() << "();\n";
        }
        else
        {
            file << "                EE_HALT(); // Error! Trying to instantiate an abstract type!\n";
            file << "                return nullptr;\n";
        }
        file << "            }\n\n";
    }

    void CodeGenerator::GenerateTypeInfoInPlaceCreationMethod( std::stringstream& file, ReflectedType const& type )
    {
        file << "            virtual void CreateTypeInPlace( IReflectedType* pAllocatedMemory ) const override final\n";
        file << "            {\n";
        if ( !type.IsAbstract() )
        {
            file << "                EE_ASSERT( pAllocatedMemory != nullptr );\n";
            file << "                new( pAllocatedMemory ) " << type.m_namespace.c_str() << type.m_name.c_str() << "();\n";
        }
        else
        {
            file << "                EE_HALT(); // Error! Trying to instantiate an abstract type!\n";
        }
        file << "            }\n\n";
    }

    void CodeGenerator::GenerateTypeInfoResetTypeMethod( std::stringstream& file, ReflectedType const& type )
    {
        file << "            virtual void ResetType( IReflectedType* pTypeInstance ) const override final\n";
        file << "            {\n";
        if ( !type.IsAbstract() )
        {
            file << "                EE_ASSERT( pTypeInstance != nullptr );\n";
            file << "                pTypeInstance->~IReflectedType();\n";
            file << "                CreateTypeInPlace( pTypeInstance );\n";
        }
        else
        {
            file << "                EE_HALT(); // Error! Trying to reset an abstract type!\n";
        }
        file << "            }\n\n";
    }

    void CodeGenerator::GenerateTypeInfoArrayAccessorMethod( std::stringstream& file, ReflectedType const& type )
    {
        file << "            virtual uint8_t* GetArrayElementDataPtr( IReflectedType* pType, uint64_t arrayID, size_t arrayIdx ) const override final\n";
        file << "            {\n";

        if ( type.HasArrayProperties() )
        {
            file << "                auto pActualType = reinterpret_cast<" << type.m_namespace.c_str() << type.m_name.c_str() << "*>( pType );\n\n";

            for ( auto& propertyDesc : type.m_properties )
            {
                if ( propertyDesc.IsDynamicArrayProperty() )
                {
                    if ( propertyDesc.m_isDevOnly )
                    {
                        file << "                #if EE_DEVELOPMENT_TOOLS\n";
                    }

                    file << "                if ( arrayID == " << propertyDesc.m_propertyID << " )\n";
                    file << "                {\n";
                    file << "                    if ( ( arrayIdx + 1 ) >= pActualType->" << propertyDesc.m_name.c_str() << ".size() )\n";
                    file << "                    {\n";
                    file << "                        pActualType->" << propertyDesc.m_name.c_str() << ".resize( arrayIdx + 1 );\n";
                    file << "                    }\n\n";
                    file << "                    return (uint8_t*) &pActualType->" << propertyDesc.m_name.c_str() << "[arrayIdx];\n";
                    file << "                }\n";

                    if ( propertyDesc.m_isDevOnly )
                    {
                        file << "                #endif\n";
                    }

                    file << "\n";
                }
                else if ( propertyDesc.IsStaticArrayProperty() )
                {
                    if ( propertyDesc.m_isDevOnly )
                    {
                        file << "                #if EE_DEVELOPMENT_TOOLS\n";
                    }

                    file << "                if ( arrayID == " << propertyDesc.m_propertyID << " )\n";
                    file << "                {\n";
                    file << "                    return (uint8_t*) &pActualType->" << propertyDesc.m_name.c_str() << "[arrayIdx];\n";
                    file << "                }\n";

                    if ( propertyDesc.m_isDevOnly )
                    {
                        file << "                #endif\n";
                    }

                    file << "\n";
                }
            }
        }

        file << "                // We should never get here since we are asking for a ptr to an invalid property\n";
        file << "                EE_UNREACHABLE_CODE();\n";
        file << "                return nullptr;\n";
        file << "            }\n\n";
    }

    void CodeGenerator::GenerateTypeInfoArraySizeMethod( std::stringstream& file, ReflectedType const& type )
    {
        file << "            virtual size_t GetArraySize( IReflectedType const* pTypeInstance, uint64_t arrayID ) const override final\n";
        file << "            {\n";

        if ( type.HasArrayProperties() )
        {
            file << "                auto pActualType = reinterpret_cast<" << type.m_namespace.c_str() << type.m_name.c_str() << " const*>( pTypeInstance );\n";
            file << "                EE_ASSERT( pActualType != nullptr );\n\n";

            for ( auto& propertyDesc : type.m_properties )
            {
                if ( propertyDesc.IsDynamicArrayProperty() )
                {
                    if ( propertyDesc.m_isDevOnly )
                    {
                        file << "                #if EE_DEVELOPMENT_TOOLS\n";
                    }

                    file << "                if ( arrayID == " << propertyDesc.m_propertyID << " )\n";
                    file << "                {\n";
                    file << "                    return pActualType->" << propertyDesc.m_name.c_str() << ".size();\n";
                    file << "                }\n";

                    if ( propertyDesc.m_isDevOnly )
                    {
                        file << "                #endif\n";
                    }

                    file << "\n";
                }
                else if ( propertyDesc.IsStaticArrayProperty() )
                {
                    if ( propertyDesc.m_isDevOnly )
                    {
                        file << "                #if EE_DEVELOPMENT_TOOLS\n";
                    }

                    file << "                if ( arrayID == " << propertyDesc.m_propertyID << " )\n";
                    file << "                {\n";
                    file << "                    return " << propertyDesc.GetArraySize() << ";\n";
                    file << "                }\n";

                    if ( propertyDesc.m_isDevOnly )
                    {
                        file << "                #endif\n";
                    }

                    file << "\n";
                }
            }
        }

        file << "                // We should never get here since we are asking for a ptr to an invalid property\n";
        file << "                EE_UNREACHABLE_CODE();\n";
        file << "                return 0;\n";
        file << "            }\n\n";
    }

    void CodeGenerator::GenerateTypeInfoArrayElementSizeMethod( std::stringstream& file, ReflectedType const& type )
    {
        file << "            virtual size_t GetArrayElementSize( uint64_t arrayID ) const override final\n";
        file << "            {\n";

        for ( auto& propertyDesc : type.m_properties )
        {
            String const templateSpecializationString = propertyDesc.m_templateArgTypeName.empty() ? String() : "<" + propertyDesc.m_templateArgTypeName + ">";

            if ( propertyDesc.IsArrayProperty() )
            {
                if ( propertyDesc.m_isDevOnly )
                {
                    file << "                #if EE_DEVELOPMENT_TOOLS\n";
                }

                file << "                if ( arrayID == " << propertyDesc.m_propertyID << " )\n";
                file << "                {\n";
                file << "                    return sizeof( " << propertyDesc.m_typeName.c_str() << templateSpecializationString.c_str() << " );\n";
                file << "                }\n";

                if ( propertyDesc.m_isDevOnly )
                {
                    file << "                #endif\n";
                }

                file << "\n";
            }
        }

        file << "                // We should never get here since we are asking for a ptr to an invalid property\n";
        file << "                EE_UNREACHABLE_CODE();\n";
        file << "                return 0;\n";
        file << "            }\n\n";
    }

    void CodeGenerator::GenerateTypeInfoArraySetSizeMethod( std::stringstream& file, ReflectedType const& type )
    {
        file << "            virtual void SetArraySize( IReflectedType* pTypeInstance, uint64_t arrayID, size_t size ) const override final\n";
        file << "            {\n";

        if ( type.HasArrayProperties() )
        {
            file << "                auto pActualType = reinterpret_cast<" << type.m_namespace.c_str() << type.m_name.c_str() << " *>( pTypeInstance );\n";
            file << "                EE_ASSERT( pActualType != nullptr );\n\n";

            for ( auto& propertyDesc : type.m_properties )
            {
                if ( propertyDesc.IsDynamicArrayProperty() )
                {
                    if ( propertyDesc.m_isDevOnly )
                    {
                        file << "                #if EE_DEVELOPMENT_TOOLS\n";
                    }

                    file << "                if ( arrayID == " << propertyDesc.m_propertyID << " )\n";
                    file << "                {\n";
                    file << "                    pActualType->" << propertyDesc.m_name.c_str() << ".resize( size );\n";
                    file << "                    return;\n";
                    file << "                }\n";

                    if ( propertyDesc.m_isDevOnly )
                    {
                        file << "                #endif\n";
                    }

                    file << "\n";
                }
            }
        }

        file << "                // We should never get here since we are asking for a ptr to an invalid property\n";
        file << "                EE_UNREACHABLE_CODE();\n";
        file << "            }\n\n";
    }

    void CodeGenerator::GenerateTypeInfoArrayClearMethod( std::stringstream& file, ReflectedType const& type )
    {
        file << "            virtual void ClearArray( IReflectedType* pTypeInstance, uint64_t arrayID ) const override final\n";
        file << "            {\n";

        if ( type.HasDynamicArrayProperties() )
        {
            file << "                auto pActualType = reinterpret_cast<" << type.m_namespace.c_str() << type.m_name.c_str() << "*>( pTypeInstance );\n";
            file << "                EE_ASSERT( pActualType != nullptr );\n\n";

            for ( auto& propertyDesc : type.m_properties )
            {
                if ( propertyDesc.IsDynamicArrayProperty() )
                {
                    if ( propertyDesc.m_isDevOnly )
                    {
                        file << "                #if EE_DEVELOPMENT_TOOLS\n";
                    }

                    file << "                if ( arrayID == " << propertyDesc.m_propertyID << " )\n";
                    file << "                {\n";
                    file << "                    pActualType->" << propertyDesc.m_name.c_str() << ".clear();\n";
                    file << "                    return;\n";
                    file << "                }\n";

                    if ( propertyDesc.m_isDevOnly )
                    {
                        file << "                #endif\n";
                    }

                    file << "\n";
                }
            }
        }

        file << "                // We should never get here since we are asking for a ptr to an invalid property\n";
        file << "                EE_UNREACHABLE_CODE();\n";
        file << "            }\n\n";
    }

    void CodeGenerator::GenerateTypeInfoAddArrayElementMethod( std::stringstream& file, ReflectedType const& type )
    {
        file << "            virtual void AddArrayElement( IReflectedType* pTypeInstance, uint64_t arrayID ) const override final\n";
        file << "            {\n";

        if ( type.HasDynamicArrayProperties() )
        {
            file << "                auto pActualType = reinterpret_cast<" << type.m_namespace.c_str() << type.m_name.c_str() << "*>( pTypeInstance );\n";
            file << "                EE_ASSERT( pActualType != nullptr );\n\n";

            for ( auto& propertyDesc : type.m_properties )
            {
                if ( propertyDesc.IsDynamicArrayProperty() )
                {
                    if ( propertyDesc.m_isDevOnly )
                    {
                        file << "                #if EE_DEVELOPMENT_TOOLS\n";
                    }

                    file << "                if ( arrayID == " << propertyDesc.m_propertyID << " )\n";
                    file << "                {\n";
                    file << "                    pActualType->" << propertyDesc.m_name.c_str() << ".emplace_back();\n";
                    file << "                    return;\n";
                    file << "                }\n";

                    if ( propertyDesc.m_isDevOnly )
                    {
                        file << "                #endif\n";
                    }

                    file << "\n";
                }
            }
        }

        file << "                // We should never get here since we are asking for a ptr to an invalid property\n";
        file << "                EE_UNREACHABLE_CODE();\n";
        file << "            }\n\n";
    }

    void CodeGenerator::GenerateTypeInfoInsertArrayElementMethod( std::stringstream& file, ReflectedType const& type )
    {
        file << "            virtual void InsertArrayElement( IReflectedType* pTypeInstance, uint64_t arrayID, size_t insertionIdx ) const override final\n";
        file << "            {\n";

        if ( type.HasDynamicArrayProperties() )
        {
            file << "                auto pActualType = reinterpret_cast<" << type.m_namespace.c_str() << type.m_name.c_str() << "*>( pTypeInstance );\n";
            file << "                EE_ASSERT( pActualType != nullptr );\n\n";

            for ( auto& propertyDesc : type.m_properties )
            {
                if ( propertyDesc.IsDynamicArrayProperty() )
                {
                    if ( propertyDesc.m_isDevOnly )
                    {
                        file << "                #if EE_DEVELOPMENT_TOOLS\n";
                    }

                    file << "                if ( arrayID == " << propertyDesc.m_propertyID << " )\n";
                    file << "                {\n";
                    file << "                    pActualType->" << propertyDesc.m_name.c_str() << ".emplace( pActualType->" << propertyDesc.m_name.c_str() << ".begin() + insertionIdx );\n";
                    file << "                    return;\n";
                    file << "                }\n";

                    if ( propertyDesc.m_isDevOnly )
                    {
                        file << "                #endif\n";
                    }

                    file << "\n";
                }
            }
        }

        file << "                // We should never get here since we are asking for a ptr to an invalid property\n";
        file << "                EE_UNREACHABLE_CODE();\n";
        file << "            }\n\n";
    }

    void CodeGenerator::GenerateTypeInfoMoveArrayElementMethod( std::stringstream& file, ReflectedType const& type )
    {
        file << "            virtual void MoveArrayElement( IReflectedType* pTypeInstance, uint64_t arrayID, size_t originalElementIdx, size_t newElementIdx ) const override final\n";
        file << "            {\n";

        if ( type.HasDynamicArrayProperties() )
        {
            file << "                auto pActualType = reinterpret_cast<" << type.m_namespace.c_str() << type.m_name.c_str() << "*>( pTypeInstance );\n";
            file << "                EE_ASSERT( pActualType != nullptr );\n\n";

            for ( auto& propertyDesc : type.m_properties )
            {
                if ( propertyDesc.IsDynamicArrayProperty() )
                {
                    if ( propertyDesc.m_isDevOnly )
                    {
                        file << "                #if EE_DEVELOPMENT_TOOLS\n";
                    }

                    file << "                if ( arrayID == " << propertyDesc.m_propertyID << " )\n";
                    file << "                {\n";
                    file << "                    auto const originalElement = pActualType->" << propertyDesc.m_name.c_str() << "[originalElementIdx];\n";
                    file << "                    pActualType->" << propertyDesc.m_name.c_str() << ".erase( pActualType->" << propertyDesc.m_name.c_str() << ".begin() + originalElementIdx );\n";
                    file << "                    pActualType->" << propertyDesc.m_name.c_str() << ".insert( pActualType->" << propertyDesc.m_name.c_str() << ".begin() + newElementIdx, originalElement );\n";
                    file << "                    return;\n";
                    file << "                }\n";

                    if ( propertyDesc.m_isDevOnly )
                    {
                        file << "                #endif\n";
                    }

                    file << "\n";
                }
            }
        }

        file << "                // We should never get here since we are asking for a ptr to an invalid property\n";
        file << "                EE_UNREACHABLE_CODE();\n";
        file << "            }\n\n";
    }

    void CodeGenerator::GenerateTypeInfoRemoveArrayElementMethod( std::stringstream& file, ReflectedType const& type )
    {
        file << "            virtual void RemoveArrayElement( IReflectedType* pTypeInstance, uint64_t arrayID, size_t elementIdx ) const override final\n";
        file << "            {\n";

        if ( type.HasDynamicArrayProperties() )
        {
            file << "                auto pActualType = reinterpret_cast<" << type.m_namespace.c_str() << type.m_name.c_str() << "*>( pTypeInstance );\n";
            file << "                EE_ASSERT( pActualType != nullptr );\n\n";

            for ( auto& propertyDesc : type.m_properties )
            {
                if ( propertyDesc.IsDynamicArrayProperty() )
                {
                    if ( propertyDesc.m_isDevOnly )
                    {
                        file << "                #if EE_DEVELOPMENT_TOOLS\n";
                    }

                    file << "                if ( arrayID == " << propertyDesc.m_propertyID << " )\n";
                    file << "                {\n";
                    file << "                    pActualType->" << propertyDesc.m_name.c_str() << ".erase( pActualType->" << propertyDesc.m_name.c_str() << ".begin() + elementIdx );\n";
                    file << "                    return;\n";
                    file << "                }\n";

                    if ( propertyDesc.m_isDevOnly )
                    {
                        file << "                #endif\n";
                    }

                    file << "\n";
                }
            }
        }

        file << "                // We should never get here since we are asking for a ptr to an invalid property\n";
        file << "                EE_UNREACHABLE_CODE();\n";
        file << "            }\n\n";
    }

    void CodeGenerator::GenerateTypeInfoCopyProperties( std::stringstream& file, ReflectedType const& type )
    {
        file << "            virtual void CopyProperties( IReflectedType* pTypeInstance, IReflectedType const* pRHS ) const override final\n";
        file << "            {\n";

        if ( type.HasProperties() )
        {
            file << "                auto pType = static_cast<" << type.m_namespace.c_str() << type.m_name.c_str() << "*>( pTypeInstance );\n";
            file << "                auto pRHSType = static_cast<" << type.m_namespace.c_str() << type.m_name.c_str() << " const*>( pRHS );\n";
            file << "                EE_ASSERT( pType != nullptr && pRHSType != nullptr );\n\n";

            for ( auto& propertyDesc : type.m_properties )
            {
                if ( propertyDesc.m_isDevOnly )
                {
                    file << "                #if EE_DEVELOPMENT_TOOLS\n";
                }

                if ( propertyDesc.IsStaticArrayProperty() )
                {
                    for ( auto i = 0u; i < propertyDesc.GetArraySize(); i++ )
                    {
                        file << "                pType->" << propertyDesc.m_name.c_str() << "[" << i << "] = pRHSType->" << propertyDesc.m_name.c_str() << "[" << i << "];\n";
                    }
                }
                else
                {
                    file << "                pType->" << propertyDesc.m_name.c_str() << " = pRHSType->" << propertyDesc.m_name.c_str() << ";\n";
                }

                if ( propertyDesc.m_isDevOnly )
                {
                    file << "                #endif\n";
                }
            }
        }

        file << "            }\n\n";
    }

    void CodeGenerator::GenerateTypeInfoAreAllPropertiesEqualMethod( std::stringstream& file, ReflectedType const& type )
    {
        file << "            virtual bool AreAllPropertyValuesEqual( IReflectedType const* pTypeInstance, IReflectedType const* pOtherTypeInstance ) const override final\n";
        file << "            {\n";

        if ( type.HasProperties() )
        {
            file << "                auto pType = reinterpret_cast<" << type.m_namespace.c_str() << type.m_name.c_str() << " const*>( pTypeInstance );\n";
            file << "                auto pOtherType = reinterpret_cast<" << type.m_namespace.c_str() << type.m_name.c_str() << " const*>( pOtherTypeInstance );\n\n";

            for ( auto& propertyDesc : type.m_properties )
            {
                if ( propertyDesc.m_isDevOnly )
                {
                    file << "                #if EE_DEVELOPMENT_TOOLS\n";
                }

                file << "                if( !IsPropertyValueEqual( pType, pOtherType, " << propertyDesc.m_propertyID << " ) )\n";
                file << "                {\n";
                file << "                    return false;\n";
                file << "                }\n";

                if ( propertyDesc.m_isDevOnly )
                {
                    file << "                #endif\n\n";
                }
                else
                {
                    file << "\n";
                }
            }
        }

        file << "                return true;\n";
        file << "            }\n\n";
    }

    void CodeGenerator::GenerateTypeInfoIsPropertyEqualMethod( std::stringstream& file, ReflectedType const& type )
    {
        file << "            virtual bool IsPropertyValueEqual( IReflectedType const* pTypeInstance, IReflectedType const* pOtherTypeInstance, uint64_t propertyID, int32_t arrayIdx = InvalidIndex ) const override final\n";
        file << "            {\n";

        if ( type.HasProperties() )
        {
            file << "                auto pType = reinterpret_cast<" << type.m_namespace.c_str() << type.m_name.c_str() << " const*>( pTypeInstance );\n";
            file << "                auto pOtherType = reinterpret_cast<" << type.m_namespace.c_str() << type.m_name.c_str() << " const*>( pOtherTypeInstance );\n\n";

            for ( auto& propertyDesc : type.m_properties )
            {
                InlineString propertyTypeName = propertyDesc.m_typeName.c_str();
                if ( !propertyDesc.m_templateArgTypeName.empty() )
                {
                    propertyTypeName += "<";
                    propertyTypeName += propertyDesc.m_templateArgTypeName.c_str();
                    propertyTypeName += ">";
                }

                //-------------------------------------------------------------------------

                if ( propertyDesc.m_isDevOnly )
                {
                    file << "                #if EE_DEVELOPMENT_TOOLS\n";
                }

                file << "                if ( propertyID == " << propertyDesc.m_propertyID << " )\n";
                file << "                {\n";

                // Arrays
                if ( propertyDesc.IsArrayProperty() )
                {
                    // Handle individual element comparison
                    //-------------------------------------------------------------------------

                    file << "                    // Compare array elements\n";
                    file << "                    if ( arrayIdx != InvalidIndex )\n";
                    file << "                    {\n";

                    // If it's a dynamic array check the sizes first
                    if ( propertyDesc.IsDynamicArrayProperty() )
                    {
                        file << "                        if ( arrayIdx >= pOtherType->" << propertyDesc.m_name.c_str() << ".size() )\n";
                        file << "                        {\n";
                        file << "                            return false;\n";
                        file << "                        }\n\n";
                    }

                    if ( propertyDesc.IsStructureProperty() )
                    {
                        file << "                        return " << propertyDesc.m_typeName.c_str() << "::s_pTypeInfo->AreAllPropertyValuesEqual( &pType->" << propertyDesc.m_name.c_str() << "[arrayIdx], &pOtherType->" << propertyDesc.m_name.c_str() << "[arrayIdx] );\n";
                    }
                    else if ( propertyDesc.IsTypeInstanceProperty() )
                    {
                        file << "                        return pType->" << propertyDesc.m_name.c_str() << "[arrayIdx].AreTypesAndPropertyValuesEqual( pOtherType->" << propertyDesc.m_name.c_str() << "[arrayIdx] );\n";
                    }
                    else
                    {
                        file << "                        return pType->" << propertyDesc.m_name.c_str() << "[arrayIdx] == pOtherType->" << propertyDesc.m_name.c_str() << "[arrayIdx];\n";
                    }
                    file << "                    }\n";

                    // Handle array comparison
                    //-------------------------------------------------------------------------

                    file << "                    else // Compare entire array contents\n";
                    file << "                    {\n";

                    // If it's a dynamic array check the sizes first
                    if ( propertyDesc.IsDynamicArrayProperty() )
                    {
                        file << "                        if ( pType->" << propertyDesc.m_name.c_str() << ".size() != pOtherType->" << propertyDesc.m_name.c_str() << ".size() )\n";
                        file << "                        {\n";
                        file << "                            return false;\n";
                        file << "                        }\n\n";

                        file << "                        for ( size_t i = 0; i < pType->" << propertyDesc.m_name.c_str() << ".size(); i++ )\n";
                    }
                    else
                    {
                        file << "                        for ( size_t i = 0; i < " << propertyDesc.GetArraySize() << "; i++ )\n";
                    }

                    file << "                        {\n";

                    if ( propertyDesc.IsStructureProperty() )
                    {
                        file << "                           if( !" << propertyDesc.m_typeName.c_str() << "::s_pTypeInfo->AreAllPropertyValuesEqual( &pType->" << propertyDesc.m_name.c_str() << "[i], &pOtherType->" << propertyDesc.m_name.c_str() << "[i] ) )\n";
                        file << "                           {\n";
                        file << "                               return false;\n";
                        file << "                           }\n";
                    }
                    else if ( propertyDesc.IsTypeInstanceProperty() )
                    {
                        file << "                           if( !pType->" << propertyDesc.m_name.c_str() << "[i].AreTypesAndPropertyValuesEqual( pOtherType->" << propertyDesc.m_name.c_str() << "[i] ) )\n";
                        file << "                           {\n";
                        file << "                               return false;\n";
                        file << "                           }\n";
                    }
                    else
                    {
                        file << "                           if( pType->" << propertyDesc.m_name.c_str() << "[i] != pOtherType->" << propertyDesc.m_name.c_str() << "[i] )\n";
                        file << "                           {\n";
                        file << "                               return false;\n";
                        file << "                           }\n";
                    }

                    file << "                        }\n\n";
                    file << "                        return true;\n";
                    file << "                    }\n";
                }
                else // Non-Array properties
                {
                    if ( propertyDesc.IsStructureProperty() )
                    {
                        file << "                    return " << propertyDesc.m_typeName.c_str() << "::s_pTypeInfo->AreAllPropertyValuesEqual( &pType->" << propertyDesc.m_name.c_str() << ", &pOtherType->" << propertyDesc.m_name.c_str() << " );\n";
                    }
                    else if ( propertyDesc.IsTypeInstanceProperty() )
                    {
                        file << "                    return pType->" << propertyDesc.m_name.c_str() << ".AreTypesAndPropertyValuesEqual( pOtherType->" << propertyDesc.m_name.c_str() << " );\n";
                    }
                    else
                    {
                        file << "                    return pType->" << propertyDesc.m_name.c_str() << " == pOtherType->" << propertyDesc.m_name.c_str() << ";\n";
                    }
                }

                file << "                }\n";

                if ( propertyDesc.m_isDevOnly )
                {
                    file << "                #endif\n";
                }

                file << "\n";
            }
        }
        else
        {
            file << "                EE_UNREACHABLE_CODE();\n";
        }

        file << "                return false;\n";
        file << "            }\n\n";
    }

    void CodeGenerator::GenerateTypeInfoSetToDefaultValueMethod( std::stringstream& file, ReflectedType const& type )
    {
        file << "            virtual void ResetToDefault( IReflectedType* pTypeInstance, uint64_t propertyID ) const override final\n";
        file << "            {\n";

        if ( type.HasProperties() )
        {
            file << "                auto pDefaultType = reinterpret_cast<" << type.m_namespace.c_str() << type.m_name.c_str() << " const*>( m_pDefaultInstance );\n";
            file << "                auto pActualType = reinterpret_cast<" << type.m_namespace.c_str() << type.m_name.c_str() << "*>( pTypeInstance );\n";
            file << "                EE_ASSERT( pActualType != nullptr && pDefaultType != nullptr );\n\n";

            for ( auto& propertyDesc : type.m_properties )
            {
                if ( propertyDesc.m_isDevOnly )
                {
                    file << "                #if EE_DEVELOPMENT_TOOLS\n";
                }

                file << "                if ( propertyID == " << propertyDesc.m_propertyID << " )\n";
                file << "                {\n";

                if ( propertyDesc.IsStaticArrayProperty() )
                {
                    for ( auto i = 0u; i < propertyDesc.GetArraySize(); i++ )
                    {
                        file << "                    pActualType->" << propertyDesc.m_name.c_str() << "[" << i << "] = pDefaultType->" << propertyDesc.m_name.c_str() << "[" << i << "];\n";
                    }
                }
                else
                {
                    file << "                    pActualType->" << propertyDesc.m_name.c_str() << " = pDefaultType->" << propertyDesc.m_name.c_str() << ";\n";
                }

                file << "                    return;\n";
                file << "                }\n";

                if ( propertyDesc.m_isDevOnly )
                {
                    file << "                #endif\n";
                }
            }
        }

        file << "            }\n";
    }

    void CodeGenerator::GenerateTypeInfoExpectedResourceTypeMethod( std::stringstream& file, ReflectedType const& type )
    {
        file << "            virtual ResourceTypeID GetExpectedResourceTypeForProperty( IReflectedType* pType, uint64_t propertyID ) const override final\n";
        file << "            {\n";

        if ( type.HasResourcePtrProperties() )
        {
            for ( auto& propertyDesc : type.m_properties )
            {
                bool const isResourceProp = ( propertyDesc.m_typeID == CoreTypeID::ResourcePtr ) || ( propertyDesc.m_typeID == CoreTypeID::TResourcePtr );
                if ( isResourceProp )
                {
                    if ( propertyDesc.m_isDevOnly )
                    {
                        file << "                #if EE_DEVELOPMENT_TOOLS\n";
                    }

                    if ( propertyDesc.m_typeID == CoreTypeID::TResourcePtr )
                    {
                        file << "                if ( propertyID == " << propertyDesc.m_propertyID << " )\n";
                        file << "                {\n";
                        file << "                    return " << propertyDesc.m_templateArgTypeName.c_str() << "::GetStaticResourceTypeID();\n";
                        file << "                }\n";
                    }
                    else if ( propertyDesc.m_typeID == CoreTypeID::ResourcePtr )
                    {
                        file << "                if ( propertyID == " << propertyDesc.m_propertyID << " )\n";
                        file << "                {\n";
                        file << "                        return ResourceTypeID();\n";
                        file << "            }\n";
                    }

                    if ( propertyDesc.m_isDevOnly )
                    {
                        file << "                #endif\n\n";
                    }
                    else
                    {
                        file << "\n";
                    }
                }
            }
        }

        file << "                // We should never get here since we are asking for a resource type of an invalid property\n";
        file << "                EE_UNREACHABLE_CODE();\n";
        file << "                return ResourceTypeID();\n";
        file << "            }\n\n";
    }

    void CodeGenerator::GenerateTypeInfoLoadResourcesMethod( std::stringstream& file, ReflectedType const& type )
    {
        file << "            virtual void LoadResources( Resource::ResourceSystem* pResourceSystem, Resource::ResourceRequesterID const& requesterID, IReflectedType* pType ) const override final\n";
        file << "            {\n";

        if ( type.HasResourcePtrOrStructProperties() )
        {
            file << "                EE_ASSERT( pResourceSystem != nullptr );\n";
            file << "                auto pActualType = reinterpret_cast<" << type.m_namespace.c_str() << type.m_name.c_str() << "*>( pType );\n\n";

            for ( auto& propertyDesc : type.m_properties )
            {
                if ( propertyDesc.m_typeID == CoreTypeID::TResourcePtr || propertyDesc.m_typeID == CoreTypeID::ResourcePtr )
                {
                    if ( propertyDesc.m_isDevOnly )
                    {
                        file << "                #if EE_DEVELOPMENT_TOOLS\n";
                    }

                    if ( propertyDesc.IsArrayProperty() )
                    {
                        if ( propertyDesc.IsDynamicArrayProperty() )
                        {
                            file << "                for ( auto& resourcePtr : pActualType->" << propertyDesc.m_name.c_str() << " )\n";
                            file << "                {\n";
                            file << "                    if ( resourcePtr.IsSet() )\n";
                            file << "                    {\n";
                            file << "                        pResourceSystem->LoadResource( resourcePtr, requesterID );\n";
                            file << "                    }\n";
                            file << "                }\n";
                        }
                        else // Static array
                        {
                            for ( auto i = 0; i < propertyDesc.m_arraySize; i++ )
                            {
                                file << "                if ( pActualType->" << propertyDesc.m_name.c_str() << "[" << i << "].IsSet() )\n";
                                file << "                {\n";
                                file << "                    pResourceSystem->LoadResource( pActualType->" << propertyDesc.m_name.c_str() << "[" << i << "], requesterID );\n";
                                file << "                }\n";
                            }
                        }
                    }
                    else
                    {
                        file << "                if ( pActualType->" << propertyDesc.m_name.c_str() << ".IsSet() )\n";
                        file << "                {\n";
                        file << "                    pResourceSystem->LoadResource( pActualType->" << propertyDesc.m_name.c_str() << ", requesterID );\n";
                        file << "                }\n";
                    }

                    if ( propertyDesc.m_isDevOnly )
                    {
                        file << "                #endif\n";
                    }
                    else
                    {
                        file << "\n";
                    }
                }
                else if ( !IsCoreType( propertyDesc.m_typeID ) && !propertyDesc.IsEnumProperty() && !propertyDesc.IsBitFlagsProperty() )
                {
                    if ( propertyDesc.m_isDevOnly )
                    {
                        file << "                #if EE_DEVELOPMENT_TOOLS\n";
                    }

                    if ( propertyDesc.IsArrayProperty() )
                    {
                        if ( propertyDesc.IsDynamicArrayProperty() )
                        {
                            file << "                for ( auto& propertyValue : pActualType->" << propertyDesc.m_name.c_str() << " )\n";
                            file << "                {\n";
                            file << "                    " << propertyDesc.m_typeName.c_str() << "::s_pTypeInfo->LoadResources( pResourceSystem, requesterID, &propertyValue );\n";
                            file << "                }\n";
                        }
                        else // Static array
                        {
                            for ( auto i = 0; i < propertyDesc.m_arraySize; i++ )
                            {
                                file << "                " << propertyDesc.m_typeName.c_str() << "::s_pTypeInfo->LoadResources( pResourceSystem, requesterID, &pActualType->" << propertyDesc.m_name.c_str() << "[" << i << "] );\n";
                            }
                        }
                    }
                    else
                    {
                        file << "                " << propertyDesc.m_typeName.c_str() << "::s_pTypeInfo->LoadResources( pResourceSystem, requesterID, &pActualType->" << propertyDesc.m_name.c_str() << " );\n";
                    }

                    if ( propertyDesc.m_isDevOnly )
                    {
                        file << "                #endif\n\n";
                    }
                    else
                    {
                        file << "\n";
                    }
                }
            }
        }

        file << "            }\n\n";
    }

    void CodeGenerator::GenerateTypeInfoUnloadResourcesMethod( std::stringstream& file, ReflectedType const& type )
    {
        file << "            virtual void UnloadResources( Resource::ResourceSystem* pResourceSystem, Resource::ResourceRequesterID const& requesterID, IReflectedType* pType ) const override final\n";
        file << "            {\n";

        if ( type.HasResourcePtrOrStructProperties() )
        {
            file << "                EE_ASSERT( pResourceSystem != nullptr );\n";
            file << "                auto pActualType = reinterpret_cast<" << type.m_namespace.c_str() << type.m_name.c_str() << "*>( pType );\n\n";

            for ( auto& propertyDesc : type.m_properties )
            {
                if ( propertyDesc.m_typeID == CoreTypeID::TResourcePtr || propertyDesc.m_typeID == CoreTypeID::ResourcePtr )
                {
                    if ( propertyDesc.m_isDevOnly )
                    {
                        file << "                #if EE_DEVELOPMENT_TOOLS\n";
                    }

                    if ( propertyDesc.IsArrayProperty() )
                    {
                        if ( propertyDesc.IsDynamicArrayProperty() )
                        {
                            file << "                for ( auto& resourcePtr : pActualType->" << propertyDesc.m_name.c_str() << " )\n";
                            file << "                {\n";
                            file << "                    if ( resourcePtr.IsSet() )\n";
                            file << "                    {\n";
                            file << "                        pResourceSystem->UnloadResource( resourcePtr, requesterID );\n";
                            file << "                    }\n";
                            file << "                }\n";
                        }
                        else // Static array
                        {
                            for ( auto i = 0; i < propertyDesc.m_arraySize; i++ )
                            {
                                file << "                if ( pActualType->" << propertyDesc.m_name.c_str() << "[" << i << "].IsSet() )\n";
                                file << "                {\n";
                                file << "                    pResourceSystem->UnloadResource( pActualType->" << propertyDesc.m_name.c_str() << "[" << i << "], requesterID );\n";
                                file << "                }\n";
                            }
                        }
                    }
                    else
                    {
                        file << "                if ( pActualType->" << propertyDesc.m_name.c_str() << ".IsSet() )\n";
                        file << "                {\n";
                        file << "                    pResourceSystem->UnloadResource( pActualType->" << propertyDesc.m_name.c_str() << ", requesterID );\n";
                        file << "                }\n";
                    }

                    if ( propertyDesc.m_isDevOnly )
                    {
                        file << "                #endif\n\n";
                    }
                    else
                    {
                        file << "\n";
                    }
                }
                else if ( !IsCoreType( propertyDesc.m_typeID ) && !propertyDesc.IsEnumProperty() && !propertyDesc.IsBitFlagsProperty() )
                {
                    if ( propertyDesc.m_isDevOnly )
                    {
                        file << "                #if EE_DEVELOPMENT_TOOLS\n";
                    }

                    if ( propertyDesc.IsArrayProperty() )
                    {
                        if ( propertyDesc.IsDynamicArrayProperty() )
                        {
                            file << "                for ( auto& propertyValue : pActualType->" << propertyDesc.m_name.c_str() << " )\n";
                            file << "                {\n";
                            file << "                    " << propertyDesc.m_typeName.c_str() << "::s_pTypeInfo->UnloadResources( pResourceSystem, requesterID, &propertyValue );\n";
                            file << "                }\n";
                        }
                        else // Static array
                        {
                            for ( auto i = 0; i < propertyDesc.m_arraySize; i++ )
                            {
                                file << "                " << propertyDesc.m_typeName.c_str() << "::s_pTypeInfo->UnloadResources( pResourceSystem, requesterID, &pActualType->" << propertyDesc.m_name.c_str() << "[" << i << "] );\n";
                            }
                        }
                    }
                    else
                    {
                        file << "                " << propertyDesc.m_typeName.c_str() << "::s_pTypeInfo->UnloadResources( pResourceSystem, requesterID, &pActualType->" << propertyDesc.m_name.c_str() << " );\n";
                    }

                    if ( propertyDesc.m_isDevOnly )
                    {
                        file << "                #endif\n\n";
                    }
                    else
                    {
                        file << "\n";
                    }
                }
            }
        }

        file << "            }\n\n";
    }

    void CodeGenerator::GenerateTypeInfoResourceLoadingStatusMethod( std::stringstream& file, ReflectedType const& type )
    {
        file << "            virtual LoadingStatus GetResourceLoadingStatus( IReflectedType* pType ) const override final\n";
        file << "            {\n";
        file << "                LoadingStatus status = LoadingStatus::Loaded;\n";

        if ( type.HasResourcePtrOrStructProperties() )
        {
            file << "\n";
            file << "                auto pActualType = reinterpret_cast<" << type.m_namespace.c_str() << type.m_name.c_str() << "*>( pType );\n\n";

            for ( auto& propertyDesc : type.m_properties )
            {
                if ( propertyDesc.m_typeID == CoreTypeID::TResourcePtr || propertyDesc.m_typeID == CoreTypeID::ResourcePtr )
                {
                    if ( propertyDesc.m_isDevOnly )
                    {
                        file << "                #if EE_DEVELOPMENT_TOOLS\n";
                    }

                    if ( propertyDesc.IsArrayProperty() )
                    {
                        if ( propertyDesc.IsDynamicArrayProperty() )
                        {
                            file << "                for ( auto const& resourcePtr : pActualType->" << propertyDesc.m_name.c_str() << " )\n";
                            file << "                {\n";
                            file << "                    if ( resourcePtr.HasLoadingFailed() )\n";
                            file << "                    {\n";
                            file << "                        status = LoadingStatus::Failed;\n";
                            file << "                    }\n";
                            file << "                    else if ( resourcePtr.IsSet() && !resourcePtr.IsLoaded() )\n";
                            file << "                    {\n";
                            file << "                        return LoadingStatus::Loading;\n";
                            file << "                    }\n";
                            file << "                }\n";
                        }
                        else // Static array
                        {
                            for ( auto i = 0; i < propertyDesc.m_arraySize; i++ )
                            {
                                file << "                if ( pActualType->" << propertyDesc.m_name.c_str() << "[" << i << "].HasLoadingFailed() )\n";
                                file << "                {\n";
                                file << "                    status = LoadingStatus::Failed;\n";
                                file << "                }\n";
                                file << "                else if ( pActualType->" << propertyDesc.m_name.c_str() << ".IsSet() && !pActualType->" << propertyDesc.m_name.c_str() << "[" << i << "].IsLoaded() )\n";
                                file << "                {\n";
                                file << "                    return LoadingStatus::Loading;\n";
                                file << "                }\n";
                            }
                        }
                    }
                    else
                    {
                        file << "                if ( pActualType->" << propertyDesc.m_name.c_str() << ".HasLoadingFailed() )\n";
                        file << "                {\n";
                        file << "                    status = LoadingStatus::Failed;\n";
                        file << "                }\n";
                        file << "                else if ( pActualType->" << propertyDesc.m_name.c_str() << ".IsSet() && !pActualType->" << propertyDesc.m_name.c_str() << ".IsLoaded() )\n";
                        file << "                {\n";
                        file << "                    return LoadingStatus::Loading;\n";
                        file << "                }\n";
                    }

                    if ( propertyDesc.m_isDevOnly )
                    {
                        file << "                #endif\n";
                    }

                    file << "\n";
                }
                else if ( !IsCoreType( propertyDesc.m_typeID ) && !propertyDesc.IsEnumProperty() && !propertyDesc.IsBitFlagsProperty() )
                {
                    if ( propertyDesc.m_isDevOnly )
                    {
                        file << "                #if EE_DEVELOPMENT_TOOLS\n";
                    }

                    if ( propertyDesc.IsArrayProperty() )
                    {
                        if ( propertyDesc.IsDynamicArrayProperty() )
                        {
                            file << "                for ( auto& propertyValue : pActualType->" << propertyDesc.m_name.c_str() << " )\n";
                            file << "                {\n";
                            file << "                    status = " << propertyDesc.m_typeName.c_str() << "::s_pTypeInfo->GetResourceLoadingStatus( &propertyValue );\n";
                            file << "                    if ( status == LoadingStatus::Loading )\n";
                            file << "                    {\n";
                            file << "                        return LoadingStatus::Loading;\n";
                            file << "                    }\n";
                            file << "                }\n";
                        }
                        else // Static array
                        {
                            for ( auto i = 0; i < propertyDesc.m_arraySize; i++ )
                            {
                                file << "                status = " << propertyDesc.m_typeName.c_str() << "::s_pTypeInfo->GetResourceLoadingStatus( &pActualType->" << propertyDesc.m_name.c_str() << "[" << i << "] ); \n";
                                file << "                if ( status == LoadingStatus::Loading )\n";
                                file << "                {\n";
                                file << "                    return LoadingStatus::Loading;\n";
                                file << "                }\n";
                            }
                        }
                    }
                    else
                    {
                        file << "                status = " << propertyDesc.m_typeName.c_str() << "::s_pTypeInfo->GetResourceLoadingStatus( &pActualType->" << propertyDesc.m_name.c_str() << " );\n";
                        file << "                if ( status == LoadingStatus::Loading )\n";
                        file << "                {\n";
                        file << "                    return LoadingStatus::Loading;\n";
                        file << "                }\n";
                    }

                    if ( propertyDesc.m_isDevOnly )
                    {
                        file << "                #endif\n";
                    }

                    file << "\n";
                }
            }
        }

        file << "                return status;\n";
        file << "            }\n\n";
    }

    void CodeGenerator::GenerateTypeInfoResourceUnloadingStatusMethod( std::stringstream& file, ReflectedType const& type )
    {
        file << "            virtual LoadingStatus GetResourceUnloadingStatus( IReflectedType* pType ) const override final\n";
        file << "            {\n";

        if ( type.HasResourcePtrOrStructProperties() )
        {
            file << "                auto pActualType = reinterpret_cast<" << type.m_namespace.c_str() << type.m_name.c_str() << "*>( pType );\n\n";

            for ( auto& propertyDesc : type.m_properties )
            {
                if ( propertyDesc.m_typeID == CoreTypeID::TResourcePtr || propertyDesc.m_typeID == CoreTypeID::ResourcePtr )
                {
                    if ( propertyDesc.m_isDevOnly )
                    {
                        file << "                #if EE_DEVELOPMENT_TOOLS\n";
                    }

                    if ( propertyDesc.IsArrayProperty() )
                    {
                        if ( propertyDesc.IsDynamicArrayProperty() )
                        {
                            file << "                for ( auto const& resourcePtr : pActualType->" << propertyDesc.m_name.c_str() << " )\n";
                            file << "                {\n";
                            file << "                    EE_ASSERT( !resourcePtr.IsLoading() );\n";
                            file << "                    if ( !resourcePtr.IsUnloaded() )\n";
                            file << "                    {\n";
                            file << "                        return LoadingStatus::Unloading;\n";
                            file << "                    }\n";
                            file << "                }\n";
                        }
                        else // Static array
                        {
                            for ( auto i = 0; i < propertyDesc.m_arraySize; i++ )
                            {
                                file << "                EE_ASSERT( !pActualType->" << propertyDesc.m_name.c_str() << "[" << i << "].IsLoading() );\n";
                                file << "                if ( !pActualType->" << propertyDesc.m_name.c_str() << "[" << i << "].IsUnloaded() )\n";
                                file << "                {\n";
                                file << "                    return LoadingStatus::Unloading;\n";
                                file << "                }\n";
                            }
                        }
                    }
                    else
                    {
                        file << "                EE_ASSERT( !pActualType->" << propertyDesc.m_name.c_str() << ".IsLoading() );\n";
                        file << "                if ( !pActualType->" << propertyDesc.m_name.c_str() << ".IsUnloaded() )\n";
                        file << "                {\n";
                        file << "                    return LoadingStatus::Unloading;\n";
                        file << "                }\n";
                    }

                    if ( propertyDesc.m_isDevOnly )
                    {
                        file << "                #endif\n";
                    }

                    file << "\n";
                }
                else if ( !IsCoreType( propertyDesc.m_typeID ) && !propertyDesc.IsEnumProperty() && !propertyDesc.IsBitFlagsProperty() )
                {
                    if ( propertyDesc.m_isDevOnly )
                    {
                        file << "                #if EE_DEVELOPMENT_TOOLS\n";
                    }

                    if ( propertyDesc.IsArrayProperty() )
                    {
                        if ( propertyDesc.IsDynamicArrayProperty() )
                        {
                            file << "                for ( auto& propertyValue : pActualType->" << propertyDesc.m_name.c_str() << " )\n";
                            file << "                {\n";
                            file << "                    LoadingStatus const status = " << propertyDesc.m_typeName.c_str() << "::s_pTypeInfo->GetResourceUnloadingStatus( &propertyValue );\n";
                            file << "                    if ( status != LoadingStatus::Unloaded )\n";
                            file << "                    {\n";
                            file << "                        return LoadingStatus::Unloading;\n";
                            file << "                    }\n";
                            file << "                }\n";
                        }
                        else // Static array
                        {
                            for ( auto i = 0; i < propertyDesc.m_arraySize; i++ )
                            {
                                file << "                if ( " << propertyDesc.m_typeName.c_str() << "::s_pTypeInfo->GetResourceUnloadingStatus( &pActualType->" << propertyDesc.m_name.c_str() << "[" << i << "] ) != LoadingStatus::Unloaded )\n";
                                file << "                {\n";
                                file << "                    return LoadingStatus::Unloading;\n";
                                file << "                }\n";

                                if ( i == propertyDesc.m_arraySize - 1 )
                                {
                                    file << "\n";
                                }
                            }
                        }
                    }
                    else
                    {
                        file << "                if ( " << propertyDesc.m_typeName.c_str() << "::s_pTypeInfo->GetResourceUnloadingStatus( &pActualType->" << propertyDesc.m_name.c_str() << " ) != LoadingStatus::Unloaded )\n";
                        file << "                {\n";
                        file << "                    return LoadingStatus::Unloading;\n";
                        file << "                }\n";
                    }

                    if ( propertyDesc.m_isDevOnly )
                    {
                        file << "                #endif\n";
                    }

                    file << "\n";
                }
            }
        }

        file << "                return LoadingStatus::Unloaded;\n";
        file << "            }\n\n";
    }

    void CodeGenerator::GenerateTypeInfoGetReferencedResourceMethod( std::stringstream& file, ReflectedType const& type )
    {
        file << "            virtual void GetReferencedResources( IReflectedType const* pType, TVector<ResourceID>& outReferencedResources ) const override final\n";
        file << "            {\n";

        if ( type.HasResourcePtrOrStructProperties() )
        {
            file << "                auto pActualType = reinterpret_cast<" << type.m_namespace.c_str() << type.m_name.c_str() << " const*>( pType );\n";

            for ( auto& propertyDesc : type.m_properties )
            {
                if ( propertyDesc.m_typeID == CoreTypeID::TResourcePtr || propertyDesc.m_typeID == CoreTypeID::ResourcePtr )
                {
                    if ( propertyDesc.m_isDevOnly )
                    {
                        file << "                #if EE_DEVELOPMENT_TOOLS\n";
                    }

                    if ( propertyDesc.IsArrayProperty() )
                    {
                        if ( propertyDesc.IsDynamicArrayProperty() )
                        {
                            file << "                for ( auto const& resourcePtr : pActualType->" << propertyDesc.m_name.c_str() << " )\n";
                            file << "                {\n";
                            file << "                    if ( resourcePtr.IsSet() )\n";
                            file << "                    {\n";
                            file << "                        outReferencedResources.emplace_back( resourcePtr.GetResourceID() );\n";
                            file << "                    }\n";
                            file << "                }\n";
                        }
                        else // Static array
                        {
                            for ( auto i = 0; i < propertyDesc.m_arraySize; i++ )
                            {
                                file << "                if ( pActualType->" << propertyDesc.m_name.c_str() << "[" << i << "].IsSet() )\n";
                                file << "                {\n";
                                file << "                    outReferencedResources.emplace_back( pActualType->" << propertyDesc.m_name.c_str() << "[" << i << "].GetResourceID() );\n";
                                file << "                }\n";
                            }
                        }
                    }
                    else
                    {
                        file << "                if ( pActualType->" << propertyDesc.m_name.c_str() << ".IsSet() )\n";
                        file << "                {\n";
                        file << "                    outReferencedResources.emplace_back( pActualType->" << propertyDesc.m_name.c_str() << ".GetResourceID() );\n";
                        file << "                }\n";
                    }

                    if ( propertyDesc.m_isDevOnly )
                    {
                        file << "                #endif\n";
                    }

                    file << "\n";
                }
                else if ( !IsCoreType( propertyDesc.m_typeID ) && !propertyDesc.IsEnumProperty() && !propertyDesc.IsBitFlagsProperty() )
                {
                    if ( propertyDesc.m_isDevOnly )
                    {
                        file << "                #if EE_DEVELOPMENT_TOOLS\n";
                    }

                    if ( propertyDesc.IsArrayProperty() )
                    {
                        if ( propertyDesc.IsDynamicArrayProperty() )
                        {
                            file << "                for ( auto& propertyValue : pActualType->" << propertyDesc.m_name.c_str() << " )\n";
                            file << "                {\n";
                            file << "                    " << propertyDesc.m_typeName.c_str() << "::s_pTypeInfo->GetReferencedResources( &propertyValue, outReferencedResources );\n";
                            file << "                }\n";
                        }
                        else // Static array
                        {
                            for ( auto i = 0; i < propertyDesc.m_arraySize; i++ )
                            {
                                file << "                " << propertyDesc.m_typeName.c_str() << "::s_pTypeInfo->GetReferencedResources( &pActualType->" << propertyDesc.m_name.c_str() << "[" << i << "], outReferencedResources ); \n";
                            }
                        }
                    }
                    else
                    {
                        file << "                " << propertyDesc.m_typeName.c_str() << "::s_pTypeInfo->GetReferencedResources( &pActualType->" << propertyDesc.m_name.c_str() << ", outReferencedResources );\n";
                    }

                    if ( propertyDesc.m_isDevOnly )
                    {
                        file << "                #endif\n";
                    }

                    file << "\n";
                }
            }
        }

        file << "            }\n\n";
    }

    void CodeGenerator::GenerateTypeInfoStaticTypeRegistrationMethods( std::stringstream& file, ReflectedType const& type )
    {
        file << "            static void RegisterType( TypeSystem::TypeRegistry& typeRegistry )\n";
        file << "            {\n";
        file << "                " << type.m_namespace.c_str() << type.m_name.c_str() << "::s_pTypeInfo = EE::New<TTypeInfo<" << type.m_namespace.c_str() << type.m_name.c_str() << ">>();\n";
        file << "                typeRegistry.RegisterType( " << type.m_namespace.c_str() << type.m_name.c_str() << "::s_pTypeInfo );\n";
        file << "            }\n\n";

        //-------------------------------------------------------------------------

        if ( !type.IsAbstract() && !type.IsEnum() )
        {
            file << "            static void CreateDefaultInstance()\n";
            file << "            {\n";
            file << "                auto pMutableTypeInfo = const_cast<TypeSystem::TypeInfo*&>( " << type.m_namespace.c_str() << type.m_name.c_str() << "::s_pTypeInfo ); \n";
            file << "                auto pMemory = EE::Alloc( sizeof( " << type.m_namespace.c_str() << type.m_name.c_str() << " ), alignof( " << type.m_namespace.c_str() << type.m_name.c_str() << " ) );\n";

            if ( type.m_hasCustomDefaultInstanceCtor )
            {
                file << "                pMutableTypeInfo->m_pDefaultInstance = new ( pMemory ) " << type.m_namespace.c_str() << type.m_name.c_str() << "( DefaultInstanceCtor );\n\n";
            }
            else
            {
                file << "                pMutableTypeInfo->m_pDefaultInstance = new ( pMemory ) " << type.m_namespace.c_str() << type.m_name.c_str() << "();\n\n";
            }

            file << "                // Set default info\n";
            file << "                //-------------------------------------------------------------------------\n\n";

            file << "                auto pDefaultInstance = reinterpret_cast<" << type.m_namespace.c_str() << type.m_name.c_str() << " const*>( pMutableTypeInfo->m_pDefaultInstance );\n";

            for ( ReflectedProperty const& prop : type.m_properties )
            {
                String const templateSpecializationString = prop.m_templateArgTypeName.empty() ? String() : "<" + prop.m_templateArgTypeName + ">";

                if ( prop.m_isDevOnly )
                {
                    file << "\n                #if EE_DEVELOPMENT_TOOLS";
                }

                file << "\n                {\n";
                file << "                    TypeSystem::PropertyInfo* pPropertyInfo = pMutableTypeInfo->GetPropertyInfo( StringID( \"" << prop.m_name.c_str() << "\" ) );\n";
                file << "                    pPropertyInfo->m_pDefaultValue = &pDefaultInstance->" << prop.m_name.c_str() << ";\n";

                if ( prop.IsDynamicArrayProperty() )
                {
                    file << "                    pPropertyInfo->m_pDefaultArrayData = pDefaultInstance->" << prop.m_name.c_str() << ".data();\n";
                    file << "                    pPropertyInfo->m_arraySize = (int32_t) pDefaultInstance->" << prop.m_name.c_str() << ".size();\n";
                    file << "                    pPropertyInfo->m_arrayElementSize = (int32_t) sizeof( " << prop.m_typeName.c_str() << templateSpecializationString.c_str() << " );\n";
                    file << "                    pPropertyInfo->m_size = sizeof( TVector<" << prop.m_typeName.c_str() << templateSpecializationString.c_str() << "> );\n";
                }
                else if ( prop.IsStaticArrayProperty() )
                {
                    file << "                    pPropertyInfo->m_pDefaultArrayData = pDefaultInstance->" << prop.m_name.c_str() << ";\n";
                    file << "                    pPropertyInfo->m_arraySize = " << prop.GetArraySize() << ";\n";
                    file << "                    pPropertyInfo->m_arrayElementSize = (int32_t) sizeof( " << prop.m_typeName.c_str() << templateSpecializationString.c_str() << " );\n";
                    file << "                    pPropertyInfo->m_size = sizeof( " << prop.m_typeName.c_str() << templateSpecializationString.c_str() << " ) * " << prop.GetArraySize() << ";\n";
                }
                else
                {
                    file << "                    pPropertyInfo->m_size = sizeof( " << prop.m_typeName.c_str() << templateSpecializationString.c_str() << " );\n";
                }
                file << "                }\n";

                if ( prop.m_isDevOnly )
                {
                    file << "                #endif\n";
                }
            }

            file << "            }\n\n";
        }

        //-------------------------------------------------------------------------

        file << "            static void UnregisterType( TypeSystem::TypeRegistry& typeRegistry )\n";
        file << "            {\n";
        file << "                typeRegistry.UnregisterType( " << type.m_namespace.c_str() << type.m_name.c_str() << "::s_pTypeInfo );\n";

        // Destroy default type instance
        if ( !type.IsAbstract() && !type.IsEnum() )
        {
            file << "                EE::Delete( const_cast<IReflectedType*&>( " << type.m_namespace.c_str() << type.m_name.c_str() << "::s_pTypeInfo->m_pDefaultInstance ) );\n";
        }

        file << "                EE::Delete( " << type.m_namespace.c_str() << type.m_name.c_str() << "::s_pTypeInfo );\n";
        file << "            }\n\n";
    }

    void CodeGenerator::GenerateTypeInfoConstructor( std::stringstream& file, ReflectedType const& type, ReflectedType const& parentType )
    {
        // The pass by value here is intentional!
        auto GeneratePropertyRegistrationCode = [this, &file, type] ( ReflectedProperty const& prop )
        {
            String const templateSpecializationString = prop.m_templateArgTypeName.empty() ? String() : "<" + prop.m_templateArgTypeName + ">";

            file << "\n";
            file << "                //-------------------------------------------------------------------------\n\n";

            if ( prop.m_isDevOnly )
            {
                file << "                #if EE_DEVELOPMENT_TOOLS\n";
            }

            file << "                propertyInfo.m_ID = StringID( \"" << prop.m_name.c_str() << "\" );\n";
            file << "                propertyInfo.m_typeID = TypeSystem::TypeID( \"" << prop.m_typeName.c_str() << "\" );\n";
            file << "                propertyInfo.m_parentTypeID = " << type.m_ID.ToUint() << ";\n";
            file << "                propertyInfo.m_templateArgumentTypeID = TypeSystem::TypeID( \"" << prop.m_templateArgTypeName.c_str() << "\" );\n";
            file << "                propertyInfo.m_offset = offsetof( " << type.m_namespace.c_str() << type.m_name.c_str() << ", " << prop.m_name.c_str() << " );\n\n";

            // Create dev tools info
            //-------------------------------------------------------------------------
            file << "                #if EE_DEVELOPMENT_TOOLS\n";

            if ( prop.m_isDevOnly )
            {
                file << "                propertyInfo.m_isForDevelopmentUseOnly = true;\n";
            }
            else
            {
                file << "                propertyInfo.m_isForDevelopmentUseOnly = false;\n";
            }

            file << "                propertyInfo.m_metadata.Clear();\n";
            file << "                propertyInfo.m_metadata.m_flags = TBitFlags<EE::TypeSystem::PropertyMetadata::Flag>( " << (uint32_t) prop.m_metaData.m_flags << " );\n";

            for ( PropertyMetadata::KV const& kv : prop.m_metaData.m_keyValues )
            {
                if ( kv.m_key == PropertyMetadata::Unknown )
                {
                    file << "                propertyInfo.m_metadata.m_keyValues.emplace_back( \"" << kv.m_keyValue.c_str() << "\", \"" << kv.m_value.c_str() << "\"" << " );\n";
                }
                else
                {
                    file << "                propertyInfo.m_metadata.m_keyValues.emplace_back( EE::TypeSystem::PropertyMetadata::Flag::" << PropertyMetadata::s_flagStrings[kv.m_key] << ", \"" << kv.m_value.c_str() << "\"" << " );\n";
                }
            }

            file << "                #endif\n\n";

            // Default Info
            //-------------------------------------------------------------------------

            // Abstract types cannot have default values since they cannot be instantiated
            // We set property info default type information once we create the default instance
            if ( type.IsAbstract() )
            {
                file << "                propertyInfo.m_pDefaultValue = nullptr;\n";
            }

            // Add property
            //-------------------------------------------------------------------------

            file << "                propertyInfo.m_flags.Set( " << prop.m_flags << " );\n";
            file << "                m_properties.emplace_back( propertyInfo );\n";
            file << "                m_propertyMap.insert( TPair<StringID, int32_t>( propertyInfo.m_ID, int32_t( m_properties.size() ) - 1 ) );\n";

            if ( prop.m_isDevOnly )
            {
                file << "                #endif\n";
            }

            return true;
        };

        //-------------------------------------------------------------------------

        file << "            TTypeInfo()\n";
        file << "            {\n";

        // Create type info
        file << "                m_ID = TypeSystem::TypeID( \"" << type.m_namespace.c_str() << type.m_name.c_str() << "\" );\n";
        file << "                m_size = sizeof( " << type.m_namespace.c_str() << type.m_name.c_str() << " );\n";
        file << "                m_alignment = alignof( " << type.m_namespace.c_str() << type.m_name.c_str() << " );\n";

        // Add type metadata
        if ( type.IsAbstract() )
        {
            file << "                m_isAbstract = true;\n";
        }

        file << "\n";

        // Create dev tools info
        file << "                #if EE_DEVELOPMENT_TOOLS\n";
        file << "                m_friendlyName = \"" << type.GetFriendlyName().c_str() << "\";\n";
        file << "                m_namespace = \"" << type.GetInternalNamespace().c_str() << "\";\n";
        file << "                m_category = \"" << type.GetCategory().c_str() << "\";\n";

        if ( type.m_isDevOnly )
        {
            file << "                m_isForDevelopmentUseOnly = true;\n";
        }

        file << "                #endif\n\n";

        // Add parent info
        file << "                // Parent types\n";
        file << "                //-------------------------------------------------------------------------\n\n";
        file << "                m_pParentTypeInfo = " << parentType.m_namespace.c_str() << parentType.m_name.c_str() << "::s_pTypeInfo;\n";

        // Add properties
        if ( type.HasProperties() )
        {
            file << "\n";
            file << "                // Register properties and type\n";
            file << "                //-------------------------------------------------------------------------\n\n";

            if ( !type.IsAbstract() )
            {
                file << "                auto pActualDefaultInstance = reinterpret_cast<" << type.m_namespace.c_str() << type.m_name.c_str() << " const*>( m_pDefaultInstance );\n";
            }

            file << "                PropertyInfo propertyInfo;\n";

            for ( auto& prop : type.m_properties )
            {
                GeneratePropertyRegistrationCode( prop );
            }
        }

        file << "            }\n\n";
    }
}