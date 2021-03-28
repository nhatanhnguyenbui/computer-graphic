#include <iostream>
#include <cmath>
#include <fstream>
#include <map>
#include <vector>
#include <unistd.h>

#include <GL/glew.h>
#include <GL/gl.h>
#include <GLFW/glfw3.h>

#include <ao/ao.h>
#include <mpg123.h>

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/gtc/matrix_transform.hpp>

using namespace std;

mpg123_handle *mh;
unsigned char *buffer;
size_t buffer_size;
size_t done;
int err;

int driver;
ao_device *dev;

ao_sample_format format;
int channels, encoding;
long rate;

void audio_init() {

    ao_initialize();
    driver = ao_default_driver_id();
    mpg123_init();
    mh = mpg123_new(NULL, &err);
    buffer_size= 3000;
    buffer = (unsigned char*) malloc(buffer_size * sizeof(unsigned char));

    mpg123_open(mh, "./audio_background.mp3");
    mpg123_getformat(mh, &rate, &channels, &encoding);

    format.bits = mpg123_encsize(encoding) * 8;
    format.rate = rate;
    format.channels = channels;
    format.byte_format = AO_FMT_NATIVE;
    format.matrix = 0;
    dev = ao_open_live(driver, &format, NULL);
}

void audio_play() {
    
    if (mpg123_read(mh, buffer, buffer_size, &done) == MPG123_OK)
        ao_play(dev, (char*) buffer, done);
    else mpg123_seek(mh, 0, SEEK_SET);
}

void audio_close() {
    
    free(buffer);
    ao_close(dev);
    mpg123_close(mh);
    mpg123_delete(mh);
    mpg123_exit();
    ao_shutdown();
}


struct VAO {
    GLuint VertexArrayID;
    GLuint VertexBuffer;
    GLuint ColorBuffer;

    GLenum PrimitiveMode;
    GLenum FillMode;
    int NumVertices;
};
typedef struct VAO VAO;

struct GLMatrices {
    glm::mat4 projection;
    glm::mat4 model;
    glm::mat4 view;
    GLuint MatrixID;
} Matrices;

int do_rot, top;
GLuint programID;
double last_update_time, current_time;
glm::vec3 rect_pos, floor_pos;
float rectangle_rotation = 0;

/* Special thanks for that guiline to help us solve a huge problem
    https://badvertex.com/2012/11/20/how-to-load-a-glsl-shader-in-opengl-using-c.html */

GLuint LoadShaders(const char * vertex_file_path,const char * fragment_file_path) {

    GLuint VertexShaderID = glCreateShader(GL_VERTEX_SHADER);
    GLuint FragmentShaderID = glCreateShader(GL_FRAGMENT_SHADER);

    // Read the vertex shader code from the file
    string VertexShaderCode;
    ifstream VertexShaderStream(vertex_file_path, ios::in);
    if(VertexShaderStream.is_open())
	{
	    string Line = "";
	    while(getline(VertexShaderStream, Line))
		VertexShaderCode += "\n" + Line;
	    VertexShaderStream.close();
	}

    // Read the fragment shader code from the file
    string FragmentShaderCode;
    ifstream FragmentShaderStream(fragment_file_path, ios::in);
    if(FragmentShaderStream.is_open()){
	string Line = "";
	while(getline(FragmentShaderStream, Line))
	    FragmentShaderCode += "\n" + Line;
	FragmentShaderStream.close();
    }

    GLint Result = GL_FALSE;
    int InfoLogLength;

    // Compile Vertex Shader
    char const * VertexSourcePointer = VertexShaderCode.c_str();
    glShaderSource(VertexShaderID, 1, &VertexSourcePointer , NULL);
    glCompileShader(VertexShaderID);

    // Check Vertex Shader
    glGetShaderiv(VertexShaderID, GL_COMPILE_STATUS, &Result);
    glGetShaderiv(VertexShaderID, GL_INFO_LOG_LENGTH, &InfoLogLength);
    vector<char> VertexShaderErrorMessage(InfoLogLength);
    glGetShaderInfoLog(VertexShaderID, InfoLogLength, NULL, &VertexShaderErrorMessage[0]);

    // Compile Fragment Shader
    char const * FragmentSourcePointer = FragmentShaderCode.c_str();
    glShaderSource(FragmentShaderID, 1, &FragmentSourcePointer , NULL);
    glCompileShader(FragmentShaderID);

    // Check Fragment Shader
    glGetShaderiv(FragmentShaderID, GL_COMPILE_STATUS, &Result);
    glGetShaderiv(FragmentShaderID, GL_INFO_LOG_LENGTH, &InfoLogLength);
    vector<char> FragmentShaderErrorMessage(InfoLogLength);
    glGetShaderInfoLog(FragmentShaderID, InfoLogLength, NULL, &FragmentShaderErrorMessage[0]);

    // Link the program
    GLuint ProgramID = glCreateProgram();
    glAttachShader(ProgramID, VertexShaderID);
    glAttachShader(ProgramID, FragmentShaderID);
    glLinkProgram(ProgramID);

    // Check the program
    glGetProgramiv(ProgramID, GL_LINK_STATUS, &Result);
    glGetProgramiv(ProgramID, GL_INFO_LOG_LENGTH, &InfoLogLength);
    vector<char> ProgramErrorMessage( max(InfoLogLength, int(1)) );
    glGetProgramInfoLog(ProgramID, InfoLogLength, NULL, &ProgramErrorMessage[0]);

    glDeleteShader(VertexShaderID);
    glDeleteShader(FragmentShaderID);

    return ProgramID;
}


void quit(GLFWwindow *window)
{
    glfwDestroyWindow(window);
    glfwTerminate();
    exit(EXIT_SUCCESS);
}

void initGLEW(void){
    glewExperimental = GL_TRUE;
    if(glewInit()!=GLEW_OK){
	fprintf(stderr,"Glew failed to initialize : %s\n", glewGetErrorString(glewInit()));
    }
    if(!GLEW_VERSION_3_3)
	fprintf(stderr, "3.3 version not available\n");
}

// Generate VAO, VBOs and return VAO handle
struct VAO* create3DObject (GLenum primitive_mode, int numVertices, const GLfloat* vertex_buffer_data, const GLfloat* color_buffer_data, GLenum fill_mode=GL_FILL)
{
    struct VAO* vao = new struct VAO;
    vao->PrimitiveMode = primitive_mode;
    vao->NumVertices = numVertices;
    vao->FillMode = fill_mode;

    // Create Vertex Array Object
    glGenVertexArrays(1, &(vao->VertexArrayID)); // VAO
    glGenBuffers (1, &(vao->VertexBuffer)); // VBO - vertices
    glGenBuffers (1, &(vao->ColorBuffer));  // VBO - colors

    glBindVertexArray (vao->VertexArrayID); // Bind the VAO 
    glBindBuffer (GL_ARRAY_BUFFER, vao->VertexBuffer); // Bind the VBO vertices 
    glBufferData (GL_ARRAY_BUFFER, 3*numVertices*sizeof(GLfloat), vertex_buffer_data, GL_STATIC_DRAW); // Copy the vertices into VBO
    glVertexAttribPointer(
                          0,                  // attribute 0. Vertices
                          3,                  // size (x,y,z)
                          GL_FLOAT,           // type
                          GL_FALSE,           // normalized?
                          0,                  // stride
                          (void*)0            // array buffer offset
                          );

    glBindBuffer (GL_ARRAY_BUFFER, vao->ColorBuffer); // Bind the VBO colors 
    glBufferData (GL_ARRAY_BUFFER, 3*numVertices*sizeof(GLfloat), color_buffer_data, GL_STATIC_DRAW);  // Copy the vertex colors
    glVertexAttribPointer(
                          1,                  // attribute 1. Color
                          3,                  // size (r,g,b)
                          GL_FLOAT,           // type
                          GL_FALSE,           // normalized?
                          0,                  // stride
                          (void*)0            // array buffer offset
                          );

    return vao;
}

/* Render the VBOs handled by VAO */
void draw3DObject (struct VAO* vao)
{
    // Change the Fill Mode for this object
    glPolygonMode (GL_FRONT_AND_BACK, vao->FillMode);

    // Bind the VAO to use
    glBindVertexArray (vao->VertexArrayID);

    // Enable Vertex Attribute 0 - 3d Vertices
    glEnableVertexAttribArray(0);

    // Bind the VBO to use
    glBindBuffer(GL_ARRAY_BUFFER, vao->VertexBuffer);

    // Enable Vertex Attribute 1 - Color
    glEnableVertexAttribArray(1);
    // Bind the VBO to use
    glBindBuffer(GL_ARRAY_BUFFER, vao->ColorBuffer);

    // Draw the geometry !
    glDrawArrays(vao->PrimitiveMode, 0, vao->NumVertices); // Starting from vertex 0; 3 vertices total -> 1 triangle
}

////////////////////////////////////////

float rectangle_rot_dir = 1;
bool rectangle_rot_status = true;
int roll_back=0, win=0;
float rect_posx, rect_posy, rect_posz;

typedef struct Sprite
{
    string name;
    float x,y,z;
    VAO* object;
    int status;
    int angle;
}Sprite;

map <string,Sprite> normals;
map <string, Sprite> cube;
map <string,Sprite> scoreboard;

// Prefered for Keyboard events 
void keyboard (GLFWwindow* window, int key, int scancode, int action, int mods)
{
    if (action == GLFW_PRESS) {
        switch (key) {
	case GLFW_KEY_ESCAPE:
	    quit(window);
	    break;
	default:
	    break;
        }
    }
}
int side_rotation=0;
int front=0,level=1;
int w_pressed,a_pressed,d_pressed,s_pressed;

void keyboardChar (GLFWwindow* window, unsigned int key)
{
    switch (key) {
        case 'q':
            quit(window);
            break;
        case 'a':
            a_pressed=1;
            break;
        case 'd':
            d_pressed=1;
            break;
        case 'w':
            w_pressed=1;
            break;
        case 's':
            s_pressed=1;
            break;
        case 't':
            top ^= 1;
            front=0;
        break;
        case ' ':
        do_rot ^= 1;
        break;
        default:
        break;
        }
}

void reshapeWindow (GLFWwindow* window, int width, int height)
{
    int fbwidth=width, fbheight=height;
    glfwGetFramebufferSize(window, &fbwidth, &fbheight);

    GLfloat fov = M_PI/2;

    glViewport (0, 0, (GLsizei) fbwidth, (GLsizei) fbheight);

    Matrices.projection = glm::perspective(fov, (GLfloat) fbwidth / (GLfloat) fbheight, 0.1f, 500.0f);
    
}

VAO *scorerectangle, *line, *rectangle, *rectangle2, *rectangle3, *floor_vao;

void createRectangle (string name) 
{
    static const GLfloat vertex_buffer_data [] = {
	-0.5, 1.0, 0.5,
	0.5, 1.0, 0.5, 
	0.5, -1.0, 0.5,
	-0.5, 1.0, 0.5, 
	-0.5, -1.0, 0.5,
	0.5, -1.0, 0.5,
	0.5, 1.0, 0.5,
	0.5, 1.0, -0.5,
	0.5, -1.0, -0.5,
	0.5, 1.0, 0.5,
	0.5, -1.0, 0.5,
	0.5, -1.0, -0.5,
	-0.5, 1.0, -0.5,
	0.5, 1.0, -0.5,
	0.5, -1.0, -0.5,
	-0.5, 1.0, -0.5,
	-0.5, -1.0, -0.5,
	0.5, -1.0, -0.5,
	-0.5, 1.0, 0.5,
	-0.5, 1.0, -0.5,
	-0.5, -1.0, -0.5, 
	-0.5, 1.0, 0.5,
	-0.5, -1.0, 0.5, 
	-0.5, -1.0, -0.5, 
	-0.5, 1.0, 0.5,
	-0.5, 1.0, -0.5, 
	0.5, 1.0, -0.5,
	-0.5, 1.0, 0.5,
	0.5, 1.0, 0.5,
	0.5, 1.0, -0.5,
	-0.5, -1.0, 0.5,
	0.5, -1.0, 0.5,
	0.5, -1.0, -0.5,
	-0.5, -1.0, 0.5, 
	-0.5, -1.0, -0.5,
	0.5, -1.0, -0.5,
    };

    GLfloat color_buffer_data [] = {
	1.0f, 1.0f, 0.0f,//yellow
	1.0f, 1.0f, 0.0f,
	1.0f, 1.0f, 0.0f,
	1.0f, 1.0f, 0.0f,
	1.0f, 1.0f, 0.0f,
	1.0f, 1.0f, 0.0f,
	1.0f, 0.0f, 1.0f,//pink
	1.0f, 0.0f, 1.0f,
	1.0f, 0.0f, 1.0f,
	1.0f, 0.0f, 1.0f,
	1.0f, 0.0f, 1.0f,
	1.0f, 0.0f, 1.0f,
	0.0f, 1.0f, 1.0f,
	0.0f, 1.0f, 1.0f,
	0.0f, 1.0f, 1.0f,
	0.0f, 1.0f, 1.0f,
	0.0f, 1.0f, 1.0f,
	0.0f, 1.0f, 1.0f,
	1.0f, 0.0f, 0.0f,
	1.0f, 0.0f, 0.0f,
	1.0f, 0.0f, 0.0f,
	1.0f, 0.0f, 0.0f,
	1.0f, 0.0f, 0.0f,
	1.0f, 0.0f, 0.0f,
	0.0f, 1.0f, 0.0f,	
	0.0f, 1.0f, 0.0f,	
	0.0f, 1.0f, 0.0f,	
	0.0f, 1.0f, 0.0f,	
	0.0f, 1.0f, 0.0f,	
	0.0f, 1.0f, 0.0f,	
	0.0f, 0.0f, 1.0f,
	0.0f, 0.0f, 1.0f,
	0.0f, 0.0f, 1.0f,
	0.0f, 0.0f, 1.0f,
	0.0f, 0.0f, 1.0f,
	0.0f, 0.0f, 1.0f,
    };

    int i;
    for(i=0;i<72;i++)
    {
        color_buffer_data[i]=(float)105/255;
    }
    for(i=72;i<108;i++)
    {
        color_buffer_data[i]=(float)211/255;
    }


    // create3DObject creates and returns a handle to a VAO that can be used later
    rectangle = create3DObject(GL_TRIANGLES, 12*3, vertex_buffer_data, color_buffer_data, GL_FILL);
    Sprite prsprite={};
    prsprite.name=name;
    prsprite.status=1;
    prsprite.object = rectangle;
    cube[name]=prsprite;
}


void createRectangle2 (string name) 
{
    static const GLfloat vertex_buffer_data [] = {
	-0.5, -1, 1.5,
	0.5, -1, 1.5, 
	0.5, -1, -0.5,
	-0.5, -1, 1.5, 
	-0.5, -1, -0.5,
	0.5, -1, -0.5,
	0.5, 0, 1.5,
	0.5, 0, -0.5,
	0.5, -1, -0.5,
	0.5, 0, 1.5,
	0.5, -1, 1.5,
	0.5, -1, -0.5,
	-0.5, 0, 1.5,
	0.5, 0, 1.5,
    0.5, 0, -0.5,
	-0.5, 0, 1.5,
	-0.5, 0, -0.5,
	0.5, 0, -0.5,
	-0.5, 0, 1.5,
	-0.5, 0, -0.5,
	-0.5, -1, -0.5, 
	-0.5, 0, 1.5,
	-0.5, -1, 1.5, 
	-0.5, -1, -0.5, 
	-0.5, 0, 1.5,
	0.5, 0, 1.5, 
	0.5, -1, 1.5,
	-0.5, 0, 1.5,
	-0.5, -1, 1.5,
	0.5, -1, 1.5,
	-0.5, 0, -0.5,
	0.5, 0, -0.5,
	0.5, -1, -0.5,
	-0.5, 0, -0.5, 
	-0.5, -1, -0.5,
	0.5, -1, -0.5,
    };

    GLfloat color_buffer_data [] = {
	1.0f, 1.0f, 0.0f,
	1.0f, 1.0f, 0.0f,
	1.0f, 1.0f, 0.0f,
	1.0f, 1.0f, 0.0f,
	1.0f, 1.0f, 0.0f,
	1.0f, 1.0f, 0.0f,
	1.0f, 0.0f, 1.0f,
	1.0f, 0.0f, 1.0f,
	1.0f, 0.0f, 1.0f,
	1.0f, 0.0f, 1.0f,
	1.0f, 0.0f, 1.0f,
	1.0f, 0.0f, 1.0f,
	0.0f, 1.0f, 1.0f,
	0.0f, 1.0f, 1.0f,
	0.0f, 1.0f, 1.0f,
	0.0f, 1.0f, 1.0f,
	0.0f, 1.0f, 1.0f,
	0.0f, 1.0f, 1.0f,
	1.0f, 0.0f, 0.0f,
	1.0f, 0.0f, 0.0f,
	1.0f, 0.0f, 0.0f,
	1.0f, 0.0f, 0.0f,
	1.0f, 0.0f, 0.0f,
	1.0f, 0.0f, 0.0f,
	0.0f, 1.0f, 0.0f,	
	0.0f, 1.0f, 0.0f,	
	0.0f, 1.0f, 0.0f,	
	0.0f, 1.0f, 0.0f,	
	0.0f, 1.0f, 0.0f,	
	0.0f, 1.0f, 0.0f,	
	0.0f, 0.0f, 1.0f,
	0.0f, 0.0f, 1.0f,
	0.0f, 0.0f, 1.0f,
	0.0f, 0.0f, 1.0f,
	0.0f, 0.0f, 1.0f,
	0.0f, 0.0f, 1.0f,
    };
    int i,j;
    for(i=24;i<36;i++)
        for(j=0;j<3;j++)
            color_buffer_data[3*i + j]=(float)211/255;
    for(i=0;i<24;i++)
        for(j=0;j<3;j++)
            color_buffer_data[3*i+j]=(float)105/255;
    rectangle2 = create3DObject(GL_TRIANGLES, 12*3, vertex_buffer_data, color_buffer_data, GL_FILL);
    Sprite prsprite={};
    prsprite.name=name;
    prsprite.status=0;
    prsprite.object = rectangle;
    cube[name]=prsprite;
}

void createRectangle3 (string name) 
{
    static const GLfloat vertex_buffer_data [] = {
	1.5,0, 0.5,
	1.5, -1, 0.5, 
	-0.5, -1, 0.5,
	1.5, 0, 0.5, 
	-0.5, 0, 0.5,
	-0.5, -1, 0.5,
	1.5, -1, 0.5,
	1.5, -1, -0.5,
	-0.5, -1, -0.5,
	1.5, -1, 0.5,
	-0.5, -1, 0.5,
	-0.5, -1, -0.5,
	-0.5, 0, -0.5,
	1.5, 0, -0.5,
        1.5, -1, -0.5,
	-0.5, 0, -0.5,
	-0.5, -1, -0.5,
	1.5, -1, -0.5,
	1.5, 0, 0.5,
	1.5, 0, -0.5,
	-0.5, 0, -0.5, 
	1.5, 0, 0.5,
	-0.5, 0, 0.5, 
	-0.5, 0, -0.5, 
	1.5, 0, 0.5,
	1.5, 0, -0.5, 
	1.5, -1,-0.5,
	1.5, 0, 0.5,
	1.5, -1, 0.5,
	1.5, -1, -0.5,
	-0.5, 0, 0.5,
	-0.5, 0, -0.5,
	-0.5, -1, -0.5,
	-0.5, 0, 0.5, 
	-0.5, -1, 0.5,
	-0.5, -1, -0.5,
    };

    GLfloat color_buffer_data [] = {
	1.0f, 1.0f, 0.0f,
	1.0f, 1.0f, 0.0f,
	1.0f, 1.0f, 0.0f,
	1.0f, 1.0f, 0.0f,
	1.0f, 1.0f, 0.0f,
	1.0f, 1.0f, 0.0f,
	1.0f, 0.0f, 1.0f,
	1.0f, 0.0f, 1.0f,
	1.0f, 0.0f, 1.0f,
	1.0f, 0.0f, 1.0f,
	1.0f, 0.0f, 1.0f,
	1.0f, 0.0f, 1.0f,
	0.0f, 1.0f, 1.0f,
	0.0f, 1.0f, 1.0f,
	0.0f, 1.0f, 1.0f,
	0.0f, 1.0f, 1.0f,
	0.0f, 1.0f, 1.0f,
	0.0f, 1.0f, 1.0f,
	1.0f, 0.0f, 0.0f,
	1.0f, 0.0f, 0.0f,
	1.0f, 0.0f, 0.0f,
	1.0f, 0.0f, 0.0f,
	1.0f, 0.0f, 0.0f,
	1.0f, 0.0f, 0.0f,
	0.0f, 1.0f, 0.0f,	
	0.0f, 1.0f, 0.0f,	
	0.0f, 1.0f, 0.0f,	
	0.0f, 1.0f, 0.0f,	
	0.0f, 1.0f, 0.0f,	
	0.0f, 1.0f, 0.0f,	
	0.0f, 0.0f, 1.0f,
	0.0f, 0.0f, 1.0f,
	0.0f, 0.0f, 1.0f,
	0.0f, 0.0f, 1.0f,
	0.0f, 0.0f, 1.0f,
	0.0f, 0.0f, 1.0f,
    };
    int i,j;
    for(i=24;i<36;i++)
        for(j=0;j<3;j++)
            color_buffer_data[3*i + j]=(float)211/255;
    for(i=0;i<24;i++)
        for(j=0;j<3;j++)
            color_buffer_data[3*i+j]=(float)105/255;

    rectangle3 = create3DObject(GL_TRIANGLES, 12*3, vertex_buffer_data, color_buffer_data, GL_FILL);
    Sprite prsprite={};
    prsprite.name=name;
    prsprite.status=0;
    prsprite.object = rectangle;
    cube[name]=prsprite;
}

void createFloor(string name, float x,float y,float z)
{

    static const GLfloat vertex_buffer_data [] = {
        (float)(x-0.5),y,(float)(z+0.5),
        (float)(x+0.5),y,(float)(z+0.5),
        (float)(x+0.5),y,(float)(z-0.5),
        (float)(x-0.5),y,(float)(z+0.5),
        (float)(x-0.5),y,(float)(z-0.5),
        (float)(x+0.5),y,(float)(z-0.5),        
    };

    if(name=="normal")
    {
        GLfloat color_buffer_data [] = {
            0.65, 0.165, 0.165,
            0.65, 0.165, 0.165,
            0.65, 0.165, 0.165,
            0.55, 0.165, 0.165,
            0.55, 0.165, 0.165,
            0.55, 0.165, 0.165,
        };
        floor_vao = create3DObject(GL_TRIANGLES, 2*3, vertex_buffer_data, color_buffer_data, GL_FILL);
    }
    else if(name=="frag")
    {
        GLfloat color_buffer_data [] = {
            1, 1, 0,
            1, 1, 0,
            1, 1, 0,
            1, 0.8, 0,
            1, 0.8, 0,
            1, 0.8, 0,
        };
        floor_vao = create3DObject(GL_TRIANGLES, 2*3, vertex_buffer_data, color_buffer_data, GL_FILL);

    }
    else if(name=="bridge")
    {

        GLfloat color_buffer_data [] = {
            0, 1, 1,
            0, 1, 1,
            0, 1, 1,
            0, 1, 0.7,
            0, 1, 0.7,
            0, 1, 0.7,
        };
        floor_vao = create3DObject(GL_TRIANGLES, 2*3, vertex_buffer_data, color_buffer_data, GL_FILL);
    }
    else if(name=="bridgebutton")
    {

        GLfloat color_buffer_data [] = {
            0, 0, 1,
            0, 0, 1,
            0, 0, 1,
            0, 0, 0.8,
            0, 0, 0.8,
            0, 0, 0.8,
        };
        floor_vao = create3DObject(GL_TRIANGLES, 2*3, vertex_buffer_data, color_buffer_data, GL_FILL);
    }
    else if(name=="goal")
    {

        GLfloat color_buffer_data [] = {
            0, 1, 0.5,
            0, 1, 0.5,
            0, 1, 0.5,
            0, 1, 0.3,
            0, 1, 0.3,
            0, 1, 0.3,
        };
        floor_vao = create3DObject(GL_TRIANGLES, 2*3, vertex_buffer_data, color_buffer_data, GL_FILL);
    }


    Sprite prsprite={};
    prsprite.name=name;
    prsprite.x=x;
    prsprite.y=y;
    prsprite.z=z;
    prsprite.object=floor_vao;
    normals[name]=prsprite;
}

float camera_rotation_angle = 90;

int normal_pos[10][10]={
    {1,0,0,0,0,0,0,0,0,0},
    {1,0,0,0,0,0,0,0,0,0},
    {1,0,0,0,0,0,0,0,0,0},
    {1,1,1,2,1,1,1,1,1,0},
    {0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0},
};
int goal_normal[10][10]={
    {0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,1},
    {0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0},
};
int frag_normal[10][10]={
    {0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0},
    {0,1,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0},
};
int bridge_normal[10][10]={
    {0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,1,1,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0},
};

int normal_pos2[10][10]={
    {1,1,1,1,1,0,0,0,0,0},
    {1,1,1,1,1,1,0,0,0,0},
    {1,1,1,1,1,0,0,0,0,0},
    {1,1,1,1,1,1,2,1,0,0},
    {0,0,0,0,1,1,1,1,0,0},
    {0,0,1,1,1,1,1,1,0,0},
    {0,0,0,1,1,1,1,1,1,1},
    {0,0,1,1,1,1,1,1,1,1},
    {0,0,0,0,0,1,1,1,0,1},
    {0,0,0,0,0,1,1,1,1,1},
};
int goal_normal2[10][10]={
    {0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,1,0},
    {0,0,0,0,0,0,0,0,0,0},
};

int frag_normal2[10][10]={
    {0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0},
    {0,1,1,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,1,1,0,0},
    {0,0,0,0,0,0,0,0,0,0},
};

int bridge_normal2[10][10]={
    {0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,1,1,1,0,0,0},
    {0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,1,1,0,0},
    {0,0,0,0,0,0,0,0,0,0},
};

void lightitup(int sc,int bit)
{
    if(bit==0)
    {
        scoreboard["up1"].status=0;
        scoreboard["cn1"].status=0;
        scoreboard["bt1"].status=0;
        scoreboard["ul1"].status=0;
        scoreboard["ur1"].status=0;
        scoreboard["br1"].status=0;
        scoreboard["bl1"].status=0;
        if(sc==2 || sc==3 || sc ==5 ||sc ==6 || sc ==7||sc ==8||sc==9||sc==0)
            scoreboard["up1"].status=1;
        if(sc==2||sc==3||sc==4||sc==5||sc==6||sc==8||sc==9)
            scoreboard["cn1"].status=1;
        if(sc==2||sc==3||sc==5||sc==6||sc==8||sc==9||sc==0)
            scoreboard["bt1"].status=1;
        if(sc==4||sc==5||sc==6||sc==8||sc==9||sc==0)
            scoreboard["ul1"].status=1;
        if(sc==1||sc==2||sc==3||sc==4||sc==7||sc==8||sc==9||sc==0)
            scoreboard["ur1"].status=1;
        if(sc==2||sc==6||sc==8||sc==0)
            scoreboard["bl1"].status=1;
        if(sc==1||sc==3||sc==4||sc==5|sc==6||sc==7||sc==8||sc==9||sc==0)
            scoreboard["br1"].status=1;
    }
    else if(bit==1)
    {
        scoreboard["up2"].status=0;
        scoreboard["cn2"].status=0;
        scoreboard["bt2"].status=0;
        scoreboard["ul2"].status=0;
        scoreboard["ur2"].status=0;
        scoreboard["br2"].status=0;
        scoreboard["bl2"].status=0;
        if(sc==2 || sc==3 || sc ==5 ||sc ==6 || sc ==7||sc ==8||sc==9||sc==0)
            scoreboard["up2"].status=1;
        if(sc==2||sc==3||sc==4||sc==5||sc==6||sc==8||sc==9)
            scoreboard["cn2"].status=1;
        if(sc==2||sc==3||sc==5||sc==6||sc==8||sc==9||sc==0)
            scoreboard["bt2"].status=1;
        if(sc==4||sc==5||sc==6||sc==8||sc==9||sc==0)
            scoreboard["ul2"].status=1;
        if(sc==1||sc==2||sc==3||sc==4||sc==7||sc==8||sc==9||sc==0)
            scoreboard["ur2"].status=1;
        if(sc==2||sc==6||sc==8||sc==0)
            scoreboard["bl2"].status=1;
        if(sc==1||sc==3||sc==4||sc==5|sc==6||sc==7||sc==8||sc==9||sc==0)
            scoreboard["br2"].status=1;
    }
}

void createScore (string name, float x,float y, float height, float width)
{
    float w = width/2.0;
    float h = height/2.0;
    GLfloat vertex_buffer_data [] =
    {
        -w,-h,0, 
        w,-h,0, 
        w, h,0, 
        w,h,0,
        -w,h,0, 
        -w,-h,0 
    };
    GLfloat color_buffer_data [] =
    {
        1,1,1, 
        1,1,1, 
        1,1,1, 
        1,1,1, 
        1,1,1, 
        1,1,1,  
    };

    scorerectangle = create3DObject(GL_TRIANGLES, 6, vertex_buffer_data, color_buffer_data, GL_FILL);
    Sprite prsprite={};
    prsprite.name=name;
    prsprite.x=x;
    prsprite.y=y;
    prsprite.status=1;
    prsprite.object=scorerectangle;
    scoreboard[name]=prsprite;
}


int bridge_stat=0, score=0, score2=0;


void draw (GLFWwindow* window, float x, float y, float w, float h,int t)
{
    int fbwidth, fbheight;
    glfwGetFramebufferSize(window, &fbwidth, &fbheight);

    glUseProgram(programID);
    
    if(t==2)
    {
        glViewport((int)(x*fbwidth), (int)(y*fbheight), (int)(w*fbwidth), (int)(h*fbheight));
        Matrices.projection = glm::ortho(-4.0f, 4.0f, -4.0f, 4.0f, 0.1f, 500.0f);
        // Eye - Location of camera. 
        glm::vec3 eye ( 0, 0, 10 );
        // Target - Where is the camera looking at.  
        glm::vec3 target (0, 0, 0);
        // Up - Up vector defines tilt of camera.  
        glm::vec3 up (0, 1, 0);

        // Compute Camera matrix (view)
        Matrices.view = glm::lookAt(eye, target, up); // Fixed camera for 2D (ortho) in XY plane
        
        // Compute ViewProject matrix as view/camera might not be changed for this frame (basic scenario)
        glm::mat4 VP;
        VP = Matrices.projection * Matrices.view;
        
        glm::mat4 MVP;	// MVP = Projection * View * Model

        // Load identity to model matrix
        Matrices.model = glm::mat4(1.0f);

        lightitup(level,0);
        lightitup(0,1);
        for(map<string,Sprite>::iterator it=scoreboard.begin();it!=scoreboard.end();it++)
        {
            string current = it->first;
            glm::mat4 translateRectangle;
            Matrices.model = glm::mat4(1.0f);

            if(scoreboard[current].status==1)
            {
                translateRectangle = glm::translate (glm::vec3(scoreboard[current].x,scoreboard[current].y,0.0));
                Matrices.model *= translateRectangle;
                MVP = VP * Matrices.model;
                glUniformMatrix4fv(Matrices.MatrixID, 1, GL_FALSE, &MVP[0][0]);
                draw3DObject(scoreboard[current].object);
            }
        }
    }
    
    if(t==1)
    {
        glViewport((int)(x*fbwidth), (int)(y*fbheight), (int)(w*fbwidth), (int)(h*fbheight));
        Matrices.projection = glm::ortho(-4.0f, 4.0f, -4.0f, 4.0f, 0.1f, 500.0f);
        // Eye - Location of camera. 
        glm::vec3 eye ( 0, 0, 10 );
        // Target - Where is the camera looking at.  
        glm::vec3 target (0, 0, 0);
        // Up - Up vector defines tilt of camera.  
        glm::vec3 up (0, 1, 0);

        // Compute Camera matrix (view)
        Matrices.view = glm::lookAt(eye, target, up); // Fixed camera for 2D (ortho) in XY plane
        
        // Compute ViewProject matrix as view/camera might not be changed for this frame (basic scenario)
        glm::mat4 VP;
        VP = Matrices.projection * Matrices.view;
        
        glm::mat4 MVP;	// MVP = Projection * View * Model

        // Load identity to model matrix
        Matrices.model = glm::mat4(1.0f);

        if(level==1)
        {
            lightitup(score%10,0);  //Ones digit
            lightitup(score/10,1); //Tens digit
        }
        else if(level==2)
        {
            lightitup(score2%10,0);
            lightitup(score2/10,1);
        }
        for(map<string,Sprite>::iterator it=scoreboard.begin();it!=scoreboard.end();it++)
        {
            string current = it->first;
            glm::mat4 translateRectangle;
            Matrices.model = glm::mat4(1.0f);

            if(scoreboard[current].status==1)
            {
                translateRectangle = glm::translate (glm::vec3(scoreboard[current].x,scoreboard[current].y,0.0));
                Matrices.model *= translateRectangle;
                MVP = VP * Matrices.model;
                glUniformMatrix4fv(Matrices.MatrixID, 1, GL_FALSE, &MVP[0][0]);
                draw3DObject(scoreboard[current].object);
            }
        }

    }
    if(t==0)
    {
        reshapeWindow(window,fbwidth,fbheight);
        glViewport((int)(x*fbwidth), (int)(y*fbheight), (int)(w*fbwidth), (int)(h*fbheight));
        float target_x=0,target_y=0,target_z=0,eye_y=5,up_y=7,up_z=0,up_x=0,eye_x=10*cos(camera_rotation_angle*M_PI/180.0f),eye_z=10*sin(camera_rotation_angle*M_PI/180.0f);

        if(top)  //Top view
        {
            eye_y=10;
            eye_x=0;
            eye_z=0;
            up_y=0;
            up_x=0;
            up_z=-1;
        }
        else  //Tower view
        {
            eye_y=7;
            up_y=3;
            up_z=0;
            up_x=0;
            eye_x = 10*cos(camera_rotation_angle*M_PI/180.0f);
            eye_z = 10*sin(camera_rotation_angle*M_PI/180.0f)-5;
        }

        // Eye - Location of camera. 
        glm::vec3 eye ( eye_x, eye_y, eye_z );
        // Target - Where is the camera looking at.  
        glm::vec3 target (target_x, target_y, target_z);
        // Up - Up vector defines tilt of camera.  
        glm::vec3 up (up_x, up_y, up_z);

        // Compute Camera matrix (view)
        Matrices.view = glm::lookAt(eye, target, up); // Fixed camera for 2D (ortho) in XY plane

        // Compute ViewProject matrix as view/camera might not be changed for this frame (basic scenario)
        glm::mat4 VP;
        VP = Matrices.projection * Matrices.view;

        // Send our transformation to the currently bound shader, in the "MVP" uniform
        // For each model you render, since the MVP will be different (at least the M part)
        glm::mat4 MVP;	// MVP = Projection * View * Model

        // Load identity to model matrix
        Matrices.model = glm::mat4(1.0f);

        int i,j;
        for(i=0;i<10;i++)
        {
            for(j=0;j<10;j++)
            {
                float k,l;
                k = (float)i;
                l = (float)j;
                Matrices.model = glm::mat4(1.0f);
                if(level==1)
                {
                    if(frag_normal[i][j]==1)
                    {
                        Matrices.model = glm::translate(glm::vec3(0.0+k,0.0,0.0+l));
                        MVP = VP * Matrices.model;
                        glUniformMatrix4fv(Matrices.MatrixID, 1, GL_FALSE, &MVP[0][0]);

                        draw3DObject(normals["frag"].object);
                    }
                    else if(bridge_normal[i][j]==1)
                    {
                        Matrices.model = glm::translate(glm::vec3(0.0+k,0.0,0.0+l));
                        MVP = VP * Matrices.model;
                        glUniformMatrix4fv(Matrices.MatrixID, 1, GL_FALSE, &MVP[0][0]);

                        if(bridge_stat)
                            draw3DObject(normals["bridge"].object);
                    }

                    else if(normal_pos[i][j]==2)
                    {
                        Matrices.model = glm::translate(glm::vec3(0.0+k,0.0,0.0+l));
                        MVP = VP * Matrices.model;
                        glUniformMatrix4fv(Matrices.MatrixID, 1, GL_FALSE, &MVP[0][0]);

                        draw3DObject(normals["bridgebutton"].object);
                    }
                    else if(normal_pos[i][j]==1)
                    {
                        Matrices.model = glm::translate(glm::vec3(0.0+k,0.0,0.0+l));
                        MVP = VP * Matrices.model;
                        glUniformMatrix4fv(Matrices.MatrixID, 1, GL_FALSE, &MVP[0][0]);

                        draw3DObject(normals["normal"].object);
                    }
                    else if(goal_normal[i][j]==1)
                    {
                        Matrices.model = glm::translate(glm::vec3(0.0+k,0.0,0.0+l));
                        MVP = VP * Matrices.model;
                        glUniformMatrix4fv(Matrices.MatrixID, 1, GL_FALSE, &MVP[0][0]);

                        draw3DObject(normals["goal"].object);

                    }
                }

                if(level==2)
                {
                    if(frag_normal2[i][j]==1)
                    {
                        Matrices.model = glm::translate(glm::vec3(0.0+k,0.0,0.0+l));
                        MVP = VP * Matrices.model;
                        glUniformMatrix4fv(Matrices.MatrixID, 1, GL_FALSE, &MVP[0][0]);

                        draw3DObject(normals["frag"].object);
                    }
                    else if(bridge_normal2[i][j]==1)
                    {
                        Matrices.model = glm::translate(glm::vec3(0.0+k,0.0,0.0+l));
                        MVP = VP * Matrices.model;
                        glUniformMatrix4fv(Matrices.MatrixID, 1, GL_FALSE, &MVP[0][0]);

                        if(bridge_stat)
                            draw3DObject(normals["bridge"].object);
                    }

                    else if(normal_pos2[i][j]==2)
                    {
                        Matrices.model = glm::translate(glm::vec3(0.0+k,0.0,0.0+l));
                        MVP = VP * Matrices.model;
                        glUniformMatrix4fv(Matrices.MatrixID, 1, GL_FALSE, &MVP[0][0]);

                        draw3DObject(normals["bridgebutton"].object);
                    }
                    else if(normal_pos2[i][j]==1)
                    {
                        Matrices.model = glm::translate(glm::vec3(0.0+k,0.0,0.0+l));
                        MVP = VP * Matrices.model;
                        glUniformMatrix4fv(Matrices.MatrixID, 1, GL_FALSE, &MVP[0][0]);

                        draw3DObject(normals["normal"].object);
                    }
                    else if(goal_normal2[i][j]==1)
                    {
                        Matrices.model = glm::translate(glm::vec3(0.0+k,0.0,0.0+l));
                        MVP = VP * Matrices.model;
                        glUniformMatrix4fv(Matrices.MatrixID, 1, GL_FALSE, &MVP[0][0]);

                        draw3DObject(normals["goal"].object);

                    }
                }
            }
        }

        // Load identity to model matrix
        Matrices.model = glm::mat4(1.0f);
        if(cube["longy"].status==1 && w_pressed==1)
        {
            cube["longy"].status=0;
            cube["longz"].status=1;
            rect_posz-=2.0;
            w_pressed=0;

            if(level==1)
                if(normal_pos[(int)rect_posx+5][(int)rect_posz+5]==2)
                {
                    bridge_stat^=1;
                    score+=1;
                }
                else
                    score+=1;
            if(level==2)
                if(normal_pos2[(int)rect_posx+5][(int)rect_posz+5]==2)
                {
                    bridge_stat^=1;
                    score2+=1;
                }
                else
                    score2+=1;
            
            Matrices.model = glm::mat4(1.0f);
            glm::mat4 translateRectangle = glm::translate (glm::vec3(rect_posx,rect_posy,rect_posz));    // glTranslatef
            Matrices.model *= translateRectangle;
            MVP = VP * Matrices.model;
            glUniformMatrix4fv(Matrices.MatrixID, 1, GL_FALSE, &MVP[0][0]);
            draw3DObject(rectangle2);

        }
        else if(cube["longy"].status==1 && s_pressed==1)
        {
            cube["longy"].status=0;
            cube["longz"].status=1;
            rect_posz+=1.0;
            s_pressed=0;

            if(level==1)
                if(normal_pos[(int)rect_posx+5][(int)rect_posz+6]==2)
                {
                    bridge_stat^=1;
                    score+=1;
                }
                else
                    score+=1;
            if(level==2)
                if(normal_pos2[(int)rect_posx+5][(int)rect_posz+6]==2)
                {
                    bridge_stat^=1;
                    score2+=1;
                }
                else
                    score2+=1;
            Matrices.model = glm::mat4(1.0f);
            glm::mat4 translateRectangle = glm::translate (glm::vec3(rect_posx,rect_posy,rect_posz));    // glTranslatef
            Matrices.model *= translateRectangle;
            MVP = VP * Matrices.model;
            glUniformMatrix4fv(Matrices.MatrixID, 1, GL_FALSE, &MVP[0][0]);
            draw3DObject(rectangle2);

        }
        else if(cube["longy"].status==1 && a_pressed==1)
        {
            cube["longy"].status=0;
            cube["longx"].status=1;
            rect_posx-=2.0;
            a_pressed=0;

            if(level==1)
                if(normal_pos[(int)rect_posx+5][(int)rect_posz+5]==2)
                {
                    bridge_stat^=1;
                    score+=1;
                }
                else
                    score+=1;
            if(level==2)
                if(normal_pos2[(int)rect_posx+5][(int)rect_posz+5]==2)
                {
                    bridge_stat^=1;
                    score2+=1;
                }
                else
                    score2+=1;
            Matrices.model = glm::mat4(1.0f);
            glm::mat4 translateRectangle = glm::translate (glm::vec3(rect_posx,rect_posy,rect_posz));    // glTranslatef
            Matrices.model *= translateRectangle;
            MVP = VP * Matrices.model;
            glUniformMatrix4fv(Matrices.MatrixID, 1, GL_FALSE, &MVP[0][0]);
            draw3DObject(rectangle3);

        }
        else if(cube["longy"].status==1 && d_pressed==1)
        {
            cube["longy"].status=0;
            cube["longx"].status=1;
            rect_posx+=1.0;
            d_pressed=0;

            if(level==1)
                if(normal_pos[(int)rect_posx+5][(int)rect_posz+5]==2)
                {
                    bridge_stat^=1;
                    score+=1;
                }
                else
                    score+=1;
            if(level==2)
                if(normal_pos2[(int)rect_posx+5][(int)rect_posz+5]==2)
                {
                    bridge_stat^=1;
                    score2+=1;
                }
                else
                    score2+=1;
            Matrices.model = glm::mat4(1.0f);
            glm::mat4 translateRectangle = glm::translate (glm::vec3(rect_posx,rect_posy,rect_posz));    // glTranslatef
            Matrices.model *= translateRectangle;
            MVP = VP * Matrices.model;
            glUniformMatrix4fv(Matrices.MatrixID, 1, GL_FALSE, &MVP[0][0]);
            draw3DObject(rectangle3);

        }
        else if(cube["longz"].status==1 && w_pressed==1)
        {
            cube["longz"].status=0;
            cube["longy"].status=1;
            rect_posz-=1.0;
            w_pressed=0;

            if(level==1)
                if(normal_pos[(int)rect_posx+5][(int)rect_posz+5]==2)
                {
                    bridge_stat^=1;
                    score+=1;
                }
                else
                    score+=1;
            if(level==2)
                if(normal_pos2[(int)rect_posx+5][(int)rect_posz+5]==2)
                {
                    bridge_stat^=1;
                    score2+=1;
                }
                else
                    score2+=1;
            
            Matrices.model = glm::mat4(1.0f);
            glm::mat4 translateRectangle = glm::translate (glm::vec3(rect_posx,rect_posy,rect_posz));    // glTranslatef
            Matrices.model *= translateRectangle;
            MVP = VP * Matrices.model;
            glUniformMatrix4fv(Matrices.MatrixID, 1, GL_FALSE, &MVP[0][0]);
            draw3DObject(rectangle);

        }
        else if(cube["longz"].status==1 && s_pressed==1)
        {
            cube["longz"].status=0;
            cube["longy"].status=1;
            rect_posz+=2.0;
            s_pressed=0;

            if(level==1)
                if(normal_pos[(int)rect_posx+5][(int)rect_posz+5]==2)
                {
                    bridge_stat^=1;
                    score+=1;
                }
                else
                    score+=1;
            else if(level==2)
                if(normal_pos2[(int)rect_posx+5][(int)rect_posz+5]==2)
                {
                    bridge_stat^=1;
                    score2+=1;
                }
                else
                    score2+=1;
            Matrices.model = glm::mat4(1.0f);
            glm::mat4 translateRectangle = glm::translate (glm::vec3(rect_posx,rect_posy,rect_posz));    // glTranslatef
            Matrices.model *= translateRectangle;
            MVP = VP * Matrices.model;
            glUniformMatrix4fv(Matrices.MatrixID, 1, GL_FALSE, &MVP[0][0]);
            draw3DObject(rectangle);

        }

        else if(cube["longz"].status==1 && a_pressed==1)
        {
            cube["longz"].status=1;
            rect_posx-=1.0;
            a_pressed=0;

            if(level==1)
                score+=1;
            else if(level==2)
                score2+=1;
            Matrices.model = glm::mat4(1.0f);
            glm::mat4 translateRectangle = glm::translate (glm::vec3(rect_posx,rect_posy,rect_posz));    // glTranslatef
            Matrices.model *= translateRectangle;
            MVP = VP * Matrices.model;
            glUniformMatrix4fv(Matrices.MatrixID, 1, GL_FALSE, &MVP[0][0]);
            draw3DObject(rectangle2);

        }
        else if(cube["longz"].status==1 && d_pressed==1)
        {
            cube["longz"].status=1;
            rect_posx+=1.0;
            d_pressed=0;
            
            if(level==1)
                score+=1;
            else
                score2+=1;
            Matrices.model = glm::mat4(1.0f);
            glm::mat4 translateRectangle = glm::translate (glm::vec3(rect_posx,rect_posy,rect_posz));    // glTranslatef
            Matrices.model *= translateRectangle;
            MVP = VP * Matrices.model;
            glUniformMatrix4fv(Matrices.MatrixID, 1, GL_FALSE, &MVP[0][0]);
            draw3DObject(rectangle2);

        }
        else if(cube["longx"].status==1 && w_pressed==1)
        {
            cube["longx"].status=1;
            rect_posz-=1.0;
            w_pressed=0;

            if(level==1)
                score+=1;
            else
                score2+=1;
            Matrices.model = glm::mat4(1.0f);
            glm::mat4 translateRectangle = glm::translate (glm::vec3(rect_posx,rect_posy,rect_posz));    // glTranslatef
            Matrices.model *= translateRectangle;
            MVP = VP * Matrices.model;
            glUniformMatrix4fv(Matrices.MatrixID, 1, GL_FALSE, &MVP[0][0]);
            draw3DObject(rectangle3);

        }
        else if(cube["longx"].status==1 && s_pressed==1)
        {
            cube["longx"].status=1;
            rect_posz+=1.0;
            s_pressed=0;

            if(level==1)
                score+=1;
            else
                score2+=1;
            Matrices.model = glm::mat4(1.0f);
            glm::mat4 translateRectangle = glm::translate (glm::vec3(rect_posx,rect_posy,rect_posz));    // glTranslatef
            Matrices.model *= translateRectangle;
            MVP = VP * Matrices.model;
            glUniformMatrix4fv(Matrices.MatrixID, 1, GL_FALSE, &MVP[0][0]);
            draw3DObject(rectangle3);

        }
        else if(cube["longx"].status==1 && a_pressed==1)
        {
            cube["longx"].status=0;
            cube["longy"].status=1;
            rect_posx-=1.0;
            a_pressed=0;

            if(level==1)
                if(normal_pos[(int)rect_posx+5][(int)rect_posz+6]==2)
                {
                    bridge_stat^=1;
                    score+=1;
                }
                else
                    score+=1;
            if(level==2)
                if(normal_pos2[(int)rect_posx+5][(int)rect_posz+6]==2)
                {
                    bridge_stat^=1;
                    score2+=1;
                }
                else
                    score2+=1;

            Matrices.model = glm::mat4(1.0f);
            glm::mat4 translateRectangle = glm::translate (glm::vec3(rect_posx,rect_posy,rect_posz));    // glTranslatef
            Matrices.model *= translateRectangle;
            MVP = VP * Matrices.model;
            glUniformMatrix4fv(Matrices.MatrixID, 1, GL_FALSE, &MVP[0][0]);
            draw3DObject(rectangle);

        }
        else if(cube["longx"].status==1 && d_pressed==1)
        {
            cube["longx"].status=0;
            cube["longy"].status=1;
            rect_posx+=2.0;
            d_pressed=0;

            if(level==1)
                if(normal_pos[(int)rect_posx+5][(int)rect_posz+6]==2)
                {
                    bridge_stat^=1;
                    score+=1;
                }
                else
                    score+=1;
            if(level==2)
                if(normal_pos2[(int)rect_posx+5][(int)rect_posz+6]==2)
                {
                    bridge_stat^=1;
                    score2+=1;
                }
                else
                    score2+=1;

            Matrices.model = glm::mat4(1.0f);
            glm::mat4 translateRectangle = glm::translate (glm::vec3(rect_posx,rect_posy,rect_posz));    // glTranslatef
            Matrices.model *= translateRectangle;
            MVP = VP * Matrices.model;
            glUniformMatrix4fv(Matrices.MatrixID, 1, GL_FALSE, &MVP[0][0]);
            draw3DObject(rectangle);

        }

        if(cube["longy"].status==1)
        {
            int status=0;
            if(level==1)
            {
                if(normal_pos[(int)rect_posx+5][(int)rect_posz+5]==0)
                {
                    if(goal_normal[(int)rect_posx+5][(int)rect_posz+5]==1)
                    {
                        status=1;
                        win=1;
                        level=2;
                    }

                    else
                    {
                        rect_posx=-5;
                        rect_posz=-5;
                        bridge_stat=0;
                        score=0;
                    }
                }
                else if(frag_normal[(int)rect_posx+5][(int)rect_posz+5]==1)
                {
                    rect_posx=-5;
                    rect_posz=-5;
                    bridge_stat=0;
                    score=0;
                }
                else if(bridge_normal[(int)rect_posx+5][(int)rect_posz+5]==1 && bridge_stat==0)
                {
                    rect_posx=-5;
                    rect_posz=-5;
                    bridge_stat=0;
                    score=0;
                }
            }
            if(level==2)
            {
                if(normal_pos2[(int)rect_posx+5][(int)rect_posz+5]==0)
                {
                    if(goal_normal2[(int)rect_posx+5][(int)rect_posz+5]==1)
                    {
                        status=1;
                        win=2;
                    }

                    else
                    {
                        rect_posx=-5;
                        rect_posz=-5;
                        bridge_stat=0;
                        score2=score;
                    }
                }
                else if(frag_normal2[(int)rect_posx+5][(int)rect_posz+5]==1)
                {
                    rect_posx=-5;
                    rect_posz=-5;
                    bridge_stat=0;
                    score2=score;
                }
                else if(bridge_normal2[(int)rect_posx+5][(int)rect_posz+5]==1 && bridge_stat==0)
                {
                    rect_posx=-5;
                    rect_posz=-5;
                    bridge_stat=0;
                    score2=score;
                }
            }
            Matrices.model = glm::mat4(1.0f);
            glm::mat4 translateRectangle = glm::translate (glm::vec3(rect_posx,rect_posy,rect_posz));    // glTranslatef
            Matrices.model *= translateRectangle;
            MVP = VP * Matrices.model;
            glUniformMatrix4fv(Matrices.MatrixID, 1, GL_FALSE, &MVP[0][0]);
            draw3DObject(rectangle);
        }
        else if(cube["longz"].status==1)
        {
            int status=0;
            Matrices.model = glm::mat4(1.0f);
            glm::mat4 translateRectangle = glm::translate (glm::vec3(rect_posx,rect_posy,rect_posz));    // glTranslatef
            Matrices.model *= translateRectangle;
            MVP = VP * Matrices.model;
            glUniformMatrix4fv(Matrices.MatrixID, 1, GL_FALSE, &MVP[0][0]);
            if(level==1)
            {
                if((normal_pos[(int)rect_posx+5][(int)rect_posz+6]==0&&goal_normal[(int)rect_posx+5][(int)rect_posz+6]!=1)|| (normal_pos[(int)rect_posx+5][(int)rect_posz+5]==0&&goal_normal[(int)rect_posz+5][(int)rect_posz+5]!=1)||rect_posz>=4 )
                {
                    rect_posx=-5;
                    rect_posz=-5;
                    status=1;
                    score=0;
                    bridge_stat=0;
                    cube["longz"].status=0;
                    cube["longy"].status=1;
                }
                else if((bridge_normal[(int)rect_posx+5][(int)rect_posz+5]==1 && bridge_stat==0)||(bridge_normal[(int)rect_posx+5][(int)rect_posz+6]==1 && bridge_stat==0))
                {
                    rect_posx=-5;
                    rect_posz=-5;
                    bridge_stat=0;
                    score=0;
                    status=1;
                    cube["longy"].status=1;
                    cube["longz"].status=0;
                }
            }
            if(level==2)
            {
                if((normal_pos2[(int)rect_posx+5][(int)rect_posz+6]==0&&goal_normal2[(int)rect_posx+5][(int)rect_posz+6]!=1)|| (normal_pos2[(int)rect_posx+5][(int)rect_posz+5]==0&&goal_normal2[(int)rect_posz+5][(int)rect_posz+5]!=1)||rect_posz>=4 )
                {
                    rect_posx=-5;
                    rect_posz=-5;
                    status=1;
                    score2=score;
                    bridge_stat=0;
                    cube["longz"].status=0;
                    cube["longy"].status=1;
                }
                else if((bridge_normal2[(int)rect_posx+5][(int)rect_posz+5]==1 && bridge_stat==0)||(bridge_normal2[(int)rect_posx+5][(int)rect_posz+6]==1 && bridge_stat==0))
                {
                    rect_posx=-5;
                    rect_posz=-5;
                    bridge_stat=0;
                    score2=score;
                    status=1;
                    cube["longy"].status=1;
                    cube["longz"].status=0;
                }
            }
            if(status==0)
                draw3DObject(rectangle2);
        }

        else if(cube["longx"].status==1)
        {
            
            int status=0;
            Matrices.model = glm::mat4(1.0f);
            glm::mat4 translateRectangle = glm::translate (glm::vec3(rect_posx,rect_posy,rect_posz));    // glTranslatef
            Matrices.model *= translateRectangle;
            MVP = VP * Matrices.model;
            glUniformMatrix4fv(Matrices.MatrixID, 1, GL_FALSE, &MVP[0][0]);
            if(level==1)
            {
                if((normal_pos[(int)(rect_posx+6)][(int)(rect_posz+5)]==0&&goal_normal[(int)rect_posx+6][(int)rect_posz+5]!=1)||(normal_pos[(int)(rect_posx+5)][(int)(rect_posz+5)]==0&&goal_normal[(int)rect_posx+5][(int)rect_posz+5]!=1)||rect_posx<-5||rect_posz<-5)
                {
                    rect_posx=-5;
                    rect_posz=-5;
                    status=1;
                    score=0;
                    bridge_stat=0;
                    cube["longx"].status=0;
                    cube["longy"].status=1;
                }
                else if((bridge_normal[(int)rect_posx+6][(int)rect_posz+5]==1 && bridge_stat==0)||(bridge_normal[(int)rect_posx+5][(int)rect_posz+5]==1 && bridge_stat==0))
                {
                    rect_posx=-5;
                    rect_posz=-5;
                    status=1;
                    score=0;
                    cube["longy"].status=1;
                    cube["longx"].status=0;
                    bridge_stat=0;
                }
            }
            if(level==2)
            {
                if((normal_pos2[(int)(rect_posx+6)][(int)(rect_posz+5)]==0&&goal_normal2[(int)rect_posx+6][(int)rect_posz+5]!=1)||(normal_pos2[(int)(rect_posx+5)][(int)(rect_posz+5)]==0&&goal_normal2[(int)rect_posx+5][(int)rect_posz+5]!=1)||rect_posx<-5||rect_posz<-5)
                {
                    rect_posx=-5;
                    rect_posz=-5;
                    status=1;
                    score2=score;
                    bridge_stat=0;
                    cube["longx"].status=0;
                    cube["longy"].status=1;
                }
                else if((bridge_normal2[(int)rect_posx+6][(int)rect_posz+5]==1 && bridge_stat==0)||(bridge_normal2[(int)rect_posx+5][(int)rect_posz+5]==1 && bridge_stat==0))
                {
                    rect_posx=-5;
                    rect_posz=-5;
                    status=1;
                    score2=score;
                    cube["longy"].status=1;
                    cube["longx"].status=0;
                    bridge_stat=0;
                }
            }
            if(status==0)
                draw3DObject(rectangle3);
        }
    }


}

GLFWwindow* initGLFW (int width, int height){
    GLFWwindow* window; 

    if (!glfwInit()) {
        exit(EXIT_FAILURE);
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    window = glfwCreateWindow(width, height, "Bloxorz - Computer Graphic Course", NULL, NULL);

    if (!window) {
	exit(EXIT_FAILURE);
        glfwTerminate();
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval( 1 );
    glfwSetFramebufferSizeCallback(window, reshapeWindow);
    glfwSetWindowSizeCallback(window, reshapeWindow);
    glfwSetWindowCloseCallback(window, quit);
    glfwSetKeyCallback(window, keyboard);     
    glfwSetCharCallback(window, keyboardChar);  
   
    return window;
}


void initGL (GLFWwindow* window, int width, int height)
{
    // Create the models
    createRectangle ("longy");
    createRectangle2 ("longz");
    createRectangle3 ("longx");

    createFloor("normal",-5,-1.0,-5);
    createFloor("frag",-5,-1.0,-5);
    createFloor("bridge",-5,-1.0,-5);
    createFloor("bridgebutton",-5,-1.0,-5);
    createFloor("goal",-5,-1.0,-5);

    createScore("up1",2,3,0.25,2);
    createScore("ul1",1,1.5,3,0.25);
    createScore("ur1",3,1.5,3,0.25);
    createScore("cn1",2,0,0.25,2);
    createScore("bl1",1,-1.5,3,0.25);
    createScore("br1",3,-1.5,3,0.25);
    createScore("bt1",2,-3,-.25,2);
    
    createScore("up2",-2,3,0.25,2);
    createScore("ul2",-3,1.5,3,0.25);
    createScore("ur2",-1,1.5,3,0.25);
    createScore("cn2",-2,0,0.25,2);
    createScore("bl2",-3,-1.5,3,0.25);
    createScore("br2",-1,-1.5,3,0.25);
    createScore("bt2",-2,-3,-.25,2);
	
    programID = LoadShaders( "Sample_GL.vert", "Sample_GL.frag" );
    Matrices.MatrixID = glGetUniformLocation(programID, "MVP");
	
    reshapeWindow (window, width, height);

    // Background color
    glClearColor (0.0, 0.0, 0.0, 0.0f); 
    glClearDepth (1.0f);

    glEnable (GL_DEPTH_TEST);
    glDepthFunc (GL_LEQUAL);
}

int main (int argc, char** argv)
{
    int width = 800;
    int height = 800;
    rect_posx = -5;
    rect_posy = 0;
    rect_posz = -5;
    floor_pos = glm::vec3(0, 0, 0);
    do_rot = 0;
    top = 0;

    GLFWwindow* window = initGLFW(width, height);
    initGLEW();
    initGL (window, width, height);
    
    audio_init();
    last_update_time = glfwGetTime();

    while (!glfwWindowShouldClose(window)) 
    {
        if(win<2)
        {

            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            current_time = glfwGetTime();
            if(do_rot && top==0 && front==0)
                camera_rotation_angle += 90*(current_time - last_update_time);
            if(camera_rotation_angle > 720)
                camera_rotation_angle -= 720;
            last_update_time = current_time;
            draw(window, 0,0,0.8,0.8,0);
            draw(window, 0.8,0.8,0.2,0.2,1);
            draw(window,0,0.8,0.2,0.2,2);
            audio_play();
           
            glfwSwapBuffers(window);
            
            glfwPollEvents();
        }
        else
        {
            cout << "You win! You took " << score2 << " moves to finish the game." << endl;
            break;
        }
    }
    audio_close();
    glfwTerminate();
}
