#include "LandCover"
#include "Coverage"
#include "SplatCatalog"
#include "SplatCoverageLegend"

#include <osgEarth/ImageLayer>
#include <osgEarthSymbology/BillboardSymbol>
#include <osgEarthSymbology/BillboardResource>

using namespace osgEarth;
using namespace osgEarth::Splat;
using namespace osgEarth::Symbology;

#define LC "[LandCover] "


bool
LandCover::configure(const ConfigOptions& conf, const osgDB::Options* dbo)
{
    LandCoverOptions in( conf );
    
    if ( in.library().isSet() )
    {
        _lib = new ResourceLibrary( "default", in.library().get() );
        if ( !_lib->initialize( dbo ) )
        {
            OE_WARN << LC << "Failed to load resource library \"" << in.library()->full() << "\"\n";
            return false;
        }
    }

    if ( in.layers().empty() )
    {
        OE_WARN << LC << "No land cover layers defined; no land cover to render\n";
    }
    else
    {
        for(int i=0; i<in.layers().size(); ++i)
        {
            osg::ref_ptr<LandCoverLayer> layer = new LandCoverLayer();
            if ( layer->configure( in.layers().at(i), dbo ) )
            {
                _layers.push_back( layer.get() );
                OE_INFO << LC << "Configured land cover layer \"" << layer->getName() << "\"\n";
            }
        }
    }

    return true;
}

//............................................................................

bool
LandCoverLayer::configure(const ConfigOptions& conf, const osgDB::Options* dbo)
{
    LandCoverLayerOptions in( conf );

    if ( in.name().isSet() )
        setName( in.name().get() );
    if ( in.lod().isSet() )
        setLOD( in.lod().get() );
    if ( in.maxDistance().isSet() )
        setMaxDistance( in.maxDistance().get() );
    if ( in.density().isSet() )
        setDensity( in.density().get() );
    if ( in.fill().isSet() )
        setFill( in.fill().get() );
    if ( in.wind().isSet() )
        setWind( in.wind().get() );
    if ( in.brightness().isSet() )
        setBrightness( in.brightness().get() );
    if ( in.contrast().isSet() )
        setContrast( in.contrast().get() );

    for(int i=0; i<in.biomes().size(); ++i)
    {
        osg::ref_ptr<LandCoverBiome> biome = new LandCoverBiome();
        if ( biome->configure( in.biomes().at(i), dbo ) )
        {
            _biomes.push_back( biome.get() );
        }
    }

    return true;
}

int
LandCoverLayer::getTotalNumBillboards() const
{
    int count = 0;
    for(int i=0; i<getBiomes().size(); ++i)
    {
        count += getBiomes().at(i)->getBillboards().size();
    }
    return count;
}

osg::Shader*
LandCoverLayer::createShader() const
{
    std::stringstream biomeBuf;
    std::stringstream billboardBuf;

    int totalBillboards = getTotalNumBillboards();

    // encode all the biome data.
    biomeBuf << 
        "struct oe_landcover_Biome { \n"
        "    int firstBillboardIndex; \n"
        "    int numBillboards; \n"
        "    float density; \n"
        "    float fill; \n"
        "}; \n"

        "const oe_landcover_Biome oe_landcover_biomes[" << getBiomes().size() << "] = oe_landcover_Biome[" << getBiomes().size() << "] ( \n";

    billboardBuf <<
        "struct oe_landcover_Billboard { \n"
        "    int arrayIndex; \n"
        "    float width; \n"
        "    float height; \n"
        "}; \n"

        "const oe_landcover_Billboard oe_landcover_billboards[" << totalBillboards << "] = oe_landcover_Billboard[" << totalBillboards << "](\n";
    
    int index = 0;
    for(int i=0; i<getBiomes().size(); ++i)
    {
        const LandCoverBiome* biome = getBiomes().at(i).get();

        biomeBuf << "    oe_landcover_Biome(" 
            << index << ", "
            << biome->getBillboards().size() 
            << ", float(" << getDensity() << ")"
            << ", float(" << getFill() << ") )";
        
        for(int j=0; j<biome->getBillboards().size(); ++j)
        {
            const LandCoverBillboard& bb = biome->getBillboards().at(j);

            billboardBuf
                << "    oe_landcover_Billboard("
                << index 
                << ", float(" << bb._width << ")"
                << ", float(" << bb._height << ")"
                << ")";
            
            ++index;
            if ( index < totalBillboards )
                billboardBuf << ",\n";
        }

        if ( (i+1) < getBiomes().size() )
            biomeBuf << ",\n";
    }

    biomeBuf
        << "\n);\n";

    billboardBuf
        << "\n); \n";

    biomeBuf 
        << "void oe_landcover_getBiome(in int biomeIndex, out oe_landcover_Biome biome) { \n"
        << "    biome = oe_landcover_biomes[biomeIndex]; \n"
        << "} \n";
        
    billboardBuf
        << "void oe_landcover_getBillboard(in int billboardIndex, out oe_landcover_Billboard billboard) { \n"
        << "    billboard = oe_landcover_billboards[billboardIndex]; \n"
        << "} \n";
    
    osg::ref_ptr<ImageLayer> layer;

    osg::Shader* shader = new osg::Shader();
    shader->setName( "LandCoverLayer" );
    shader->setShaderSource( Stringify() << "#version 330\n" << biomeBuf.str() << "\n" << billboardBuf.str() );
    return shader;
}

osg::Shader*
LandCoverLayer::createPredicateShader(const Coverage* coverage) const
{
    const char* defaultCode = "int oe_landcover_getBiomeIndex(in vec4 coords) { return -1; }\n";

    std::stringstream buf;
    buf << "#version 330\n";
    
    osg::ref_ptr<ImageLayer> layer;

    if ( !coverage )
    {
        buf << defaultCode;
        OE_INFO << LC << "No coverage; generating default coverage predicate\n";
    }
    else if ( !coverage->getLegend() )
    {
        buf << defaultCode;
        OE_INFO << LC << "No legend; generating default coverage predicate\n";
    }
    else if ( !coverage->lockLayer(layer) )
    {
        buf << defaultCode;
        OE_INFO << LC << "No classification layer; generating default coverage predicate\n";
    }
    else
    {
        const std::string& sampler = layer->shareTexUniformName().get();
        const std::string& matrix  = layer->shareTexMatUniformName().get();

        buf << "uniform sampler2D " << sampler << ";\n"
            << "uniform mat4 " << matrix << ";\n"
            << "int oe_landcover_getBiomeIndex(in vec4 coords) { \n"
            << "    float value = textureLod(" << sampler << ", (" << matrix << " * coords).st, 0).r;\n";

        for(int biomeIndex=0; biomeIndex<getBiomes().size(); ++biomeIndex)
        {
            const LandCoverBiome* biome = getBiomes().at(biomeIndex).get();

            if ( !biome->getClasses().empty() )
            {
                StringVector classes;
                StringTokenizer(biome->getClasses(), classes, " ", "\"", false);

                for(int i=0; i<classes.size(); ++i)
                {
                    std::vector<const CoverageValuePredicate*> predicates;
                    if ( coverage->getLegend()->getPredicatesForClass(classes[i], predicates) )
                    {
                        for(std::vector<const CoverageValuePredicate*>::const_iterator p = predicates.begin();
                            p != predicates.end(); 
                            ++p)
                        {
                            const CoverageValuePredicate* predicate = *p;

                            if ( predicate->_exactValue.isSet() )
                            {
                                buf << "    if (value == " << predicate->_exactValue.get() << ") return " << biomeIndex << "; \n";
                            }
                            else if ( predicate->_minValue.isSet() && predicate->_maxValue.isSet() )
                            {
                                buf << "    if (value >= " << predicate->_minValue.get() << " && value <= " << predicate->_maxValue.get() << ") return " << biomeIndex << "; \n";
                            }
                            else if ( predicate->_minValue.isSet() )
                            {
                                buf << "    if (value >= " << predicate->_minValue.get() << ")  return " << biomeIndex << "; \n";
                            }
                            else if ( predicate->_maxValue.isSet() )
                            {
                                buf << "    if (value <= " << predicate->_maxValue.get() << ") return " << biomeIndex << "; \n";
                            }

                            else 
                            {
                                OE_WARN << LC << "Class \"" << classes[i] << "\" found, but no exact/min/max value was set in the legend\n";
                            }
                        }
                    }
                    else
                    {
                        OE_WARN << LC << "Class \"" << classes[i] << "\" not found in the legend!\n";
                    }
                }
            }

            buf << "    return -1; \n";
        }

        buf << "}\n";
    }
    
    osg::Shader* shader = new osg::Shader();
    shader->setName("oe Landcover predicate function");
    shader->setShaderSource( buf.str() );

    return shader;
}

//............................................................................

bool
LandCoverBiome::configure(const ConfigOptions& conf, const osgDB::Options* dbo)
{
    LandCoverBiomeOptions in( conf );

    if ( in.biomeClasses().isSet() )
        setClasses( in.biomeClasses().get() );

    for(SymbolVector::const_iterator i = in.symbols().begin(); i != in.symbols().end(); ++i)
    {
        const BillboardSymbol* bs = dynamic_cast<BillboardSymbol*>( i->get() );
        if ( bs )
        {
            osg::Image* image = const_cast<osg::Image*>( bs->getImage() );
            if ( !image )
            {
                image = URI(bs->url()->eval()).getImage(dbo);
            }

            if ( image )
            {
                getBillboards().push_back( LandCoverBillboard(image, bs->width().get(), bs->height().get()) );
            }
            else
            {
                OE_WARN << LC << "Failed to load billboard image from \"" << bs->url()->eval() << "\"\n";
            }
        } 
        else
        {
            OE_WARN << LC << "Unrecognized symbol in land cover biome\n";
        }
    }

    return true;
}

osg::Shader*
LandCoverBiome::createPredicateShader(const Coverage* coverage) const
{
    const char* defaultCode = "bool oe_landcover_passesCoverage(in vec4 coords) { return true; }\n";

    std::stringstream buf;
    buf << "#version 330\n";
    
    osg::ref_ptr<ImageLayer> layer;

    if ( !coverage )
    {
        buf << defaultCode;
        OE_INFO << LC << "No coverage; generating default coverage predicate\n";
    }
    else if ( !coverage->getLegend() )
    {
        buf << defaultCode;
        OE_INFO << LC << "No legend; generating default coverage predicate\n";
    }
    else if ( !coverage->lockLayer(layer) )
    {
        buf << defaultCode;
        OE_INFO << LC << "No classification layer; generating default coverage predicate\n";
    }
    else
    {
        const std::string& sampler = layer->shareTexUniformName().get();
        const std::string& matrix  = layer->shareTexMatUniformName().get();

        buf << "uniform sampler2D " << sampler << ";\n"
            << "uniform mat4 " << matrix << ";\n"
            << "bool oe_landcover_passesCoverage(in vec4 coords) { \n";
    
        if ( !getClasses().empty() )
        {
            buf << "    float value = textureLod(" << sampler << ", (" << matrix << " * coords).st, 0).r;\n";

            StringVector classes;
            StringTokenizer(getClasses(), classes, " ", "\"", false);

            for(int i=0; i<classes.size(); ++i)
            {
                std::vector<const CoverageValuePredicate*> predicates;
                if ( coverage->getLegend()->getPredicatesForClass(classes[i], predicates) )
                {
                    for(std::vector<const CoverageValuePredicate*>::const_iterator p = predicates.begin();
                        p != predicates.end(); 
                        ++p)
                    {
                        const CoverageValuePredicate* predicate = *p;

                        if ( predicate->_exactValue.isSet() )
                        {
                            buf << "    if (value == " << predicate->_exactValue.get() << ") return true;\n";
                        }
                        else if ( predicate->_minValue.isSet() && predicate->_maxValue.isSet() )
                        {
                            buf << "    if (value >= " << predicate->_minValue.get() << " && value <= " << predicate->_maxValue.get() << ") return true;\n";
                        }
                        else if ( predicate->_minValue.isSet() )
                        {
                            buf << "    if (value >= " << predicate->_minValue.get() << ") return true;\n";
                        }
                        else if ( predicate->_maxValue.isSet() )
                        {
                            buf << "    if (value <= " << predicate->_maxValue.get() << ") return true;\n";
                        }

                        else 
                        {
                            OE_WARN << LC << "Class \"" << classes[i] << "\" found, but no exact/min/max value was set in the legend\n";
                        }
                    }
                }
                else
                {
                    OE_WARN << LC << "Class \"" << classes[i] << "\" not found in the legend!\n";
                }
            }

            buf << "    return false; \n";
        }

        else
        {
            // no classes defined; accept all.
            buf << "    return true;\n";
        }

        buf << "}\n";
    }
    
    osg::Shader* shader = new osg::Shader();
    shader->setName("oe Landcover predicate function");
    shader->setShaderSource( buf.str() );

    return shader;
}
