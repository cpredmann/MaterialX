void sx_mixedf(EDF in1, float weight1, EDF in2, float weight2, out EDF result)
{
    result = in1 * weight1 + in2 * weight2;
}