resource "aws_instance" "ilb" {
    ami                         = data.aws_ami.sliderule_cluster_ami.id
    availability_zone           = var.availability_zone
    ebs_optimized               = false
    instance_type               = "c7g.large"
    monitoring                  = false
    key_name                    = var.key_pair_name
    vpc_security_group_ids      = [aws_security_group.ilb-sg.id]
    subnet_id                   = aws_subnet.sliderule-subnet.id
    associate_public_ip_address = true
    source_dest_check           = true
    iam_instance_profile        = aws_iam_instance_profile.s3-role.name
    private_ip                  = var.ilb_ip
    root_block_device {
      volume_type               = "gp2"
      volume_size               = 40
      delete_on_termination     = true
    }
    tags = {
      "Name" = "${var.cluster_name}-ilb"
    }
    user_data = <<-EOF
      #!/bin/bash
      aws ecr get-login-password --region us-west-2 | docker login --username AWS --password-stdin ${var.container_repo}
      export OAUTH_HMAC_SECRET='${local.secrets.jwt_secret_key}'
      export IS_PUBLIC=${var.is_public}
      export CLUSTER=${var.cluster_name}
      export DOMAIN=${var.domain}
      export ILB_IMAGE=${var.container_repo}/ilb:${var.cluster_version}
      mkdir -p /etc/ssl/private
      aws s3 cp s3://sliderule/config/slideruleearth.io.pem /etc/ssl/private/slideruleearth.io.pem
      aws s3 cp s3://sliderule/config/testsliderule.org.pem /etc/ssl/private/testsliderule.org.pem
      aws s3 cp s3://sliderule/infrastructure/software/${var.cluster_name}-docker-compose-ilb.yml ./docker-compose.yml
      docker-compose -p cluster up --detach
    EOF
}

# Route 53

data "aws_route53_zone" "selected" {
  name        = "${var.domain}"
}

resource "aws_route53_record" "org" {
  zone_id         = data.aws_route53_zone.selected.zone_id
  name            = "${var.cluster_name}.${data.aws_route53_zone.selected.name}"
  type            = "A"
  ttl             = 300
  allow_overwrite = true
  records         = [aws_instance.ilb.public_ip]
}